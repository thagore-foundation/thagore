use thagore_ast::{
    BinOp as AstBinOp, BinaryExpr, Block, CallExpr, Decl, Expr, ExprStmt, FieldAccessExpr,
    FieldDef, FlowDecl, FlowStage, ForStmt, FuncDecl, IdentExpr, IfStmt, IndexExpr, InternedStr,
    LetDecl, LitExpr, Literal, NodeId, Param, ReturnStmt, Span, Stmt, StructDecl, TypeExpr,
    UnaryExpr, UnaryOp as AstUnaryOp, WhileStmt,
};
use thagore_ir::{
    validate_function, validate_module, BasicBlock, BinOp, BlockId, Const, Instr, IrFunction,
    IrLowerer, Terminator, UnOp, Value, ARRAY_LEN_INTRINSIC,
};
use thagore_typeck::{TypeArena, TypeId, TypeTable};

fn span() -> Span {
    Span::new(0, 0)
}

fn leak_value<T>(value: T) -> &'static T {
    Box::leak(Box::new(value))
}

fn leak_slice<T>(items: Vec<T>) -> &'static [T] {
    Box::leak(items.into_boxed_slice())
}

struct AstFactory {
    next_id: u32,
}

impl AstFactory {
    fn new() -> Self {
        Self { next_id: 0 }
    }

    fn id(&mut self) -> NodeId {
        let id = NodeId::new(self.next_id);
        self.next_id += 1;
        id
    }

    fn expr(&self, expr: Expr<'static>) -> &'static Expr<'static> {
        leak_value(expr)
    }

    fn block(&mut self, statements: Vec<Stmt<'static>>) -> &'static Block<'static> {
        leak_value(Block {
            id: self.id(),
            span: span(),
            statements: leak_slice(statements),
        })
    }

    fn ident(&mut self, name: InternedStr) -> &'static Expr<'static> {
        let id = self.id();
        self.expr(Expr::Ident(IdentExpr {
            id,
            span: span(),
            name,
        }))
    }

    fn int(&mut self, value: i64) -> &'static Expr<'static> {
        let id = self.id();
        self.expr(Expr::Literal(LitExpr {
            id,
            span: span(),
            literal: Literal::Int(value),
        }))
    }

    fn bool_lit(&mut self, value: bool) -> &'static Expr<'static> {
        let id = self.id();
        self.expr(Expr::Literal(LitExpr {
            id,
            span: span(),
            literal: Literal::Bool(value),
        }))
    }

    fn str_lit(&mut self, value: InternedStr) -> &'static Expr<'static> {
        let id = self.id();
        self.expr(Expr::Literal(LitExpr {
            id,
            span: span(),
            literal: Literal::Str(value),
        }))
    }

    fn binary(
        &mut self,
        left: &'static Expr<'static>,
        op: AstBinOp,
        right: &'static Expr<'static>,
    ) -> &'static Expr<'static> {
        let id = self.id();
        self.expr(Expr::Binary(BinaryExpr {
            id,
            span: span(),
            left,
            op,
            right,
        }))
    }

    fn unary(&mut self, op: AstUnaryOp, operand: &'static Expr<'static>) -> &'static Expr<'static> {
        let id = self.id();
        self.expr(Expr::Unary(UnaryExpr {
            id,
            span: span(),
            op,
            operand,
        }))
    }

    fn call(
        &mut self,
        callee: &'static Expr<'static>,
        args: Vec<&'static Expr<'static>>,
    ) -> &'static Expr<'static> {
        let id = self.id();
        self.expr(Expr::Call(CallExpr {
            id,
            span: span(),
            callee,
            args: leak_slice(args),
        }))
    }

    fn field(
        &mut self,
        object: &'static Expr<'static>,
        field: InternedStr,
    ) -> &'static Expr<'static> {
        let id = self.id();
        self.expr(Expr::FieldAccess(FieldAccessExpr {
            id,
            span: span(),
            object,
            field,
        }))
    }

    fn index(
        &mut self,
        object: &'static Expr<'static>,
        index: &'static Expr<'static>,
    ) -> &'static Expr<'static> {
        let id = self.id();
        self.expr(Expr::Index(IndexExpr {
            id,
            span: span(),
            object,
            index,
        }))
    }

    fn assign(
        &mut self,
        target: &'static Expr<'static>,
        value: &'static Expr<'static>,
    ) -> &'static Expr<'static> {
        let id = self.id();
        self.expr(Expr::Assign(thagore_ast::AssignExpr {
            id,
            span: span(),
            target,
            value,
        }))
    }
}

fn expr_stmt(ast: &mut AstFactory, expr: &'static Expr<'static>) -> Stmt<'static> {
    Stmt::Expr(ExprStmt {
        id: ast.id(),
        span: span(),
        expr,
    })
}

fn return_stmt(ast: &mut AstFactory, value: Option<&'static Expr<'static>>) -> Stmt<'static> {
    Stmt::Return(ReturnStmt {
        id: ast.id(),
        span: span(),
        value,
    })
}

fn named_type(ast: &mut AstFactory, name: InternedStr) -> &'static TypeExpr<'static> {
    leak_value(TypeExpr::Named(thagore_ast::NamedTypeExpr {
        id: ast.id(),
        span: span(),
        name,
    }))
}

#[derive(Clone, Copy)]
struct Symbols {
    module: InternedStr,
    i32_: InternedStr,
    array_: InternedStr,
    point: InternedStr,
    x: InternedStr,
    y: InternedStr,
    value: InternedStr,
    items: InternedStr,
    item: InternedStr,
    foo: InternedStr,
    bar: InternedStr,
    add: InternedStr,
    stage: InternedStr,
    compensate: InternedStr,
    text: InternedStr,
}

fn symbols() -> Symbols {
    Symbols {
        module: InternedStr::new(1),
        i32_: InternedStr::new(2),
        array_: InternedStr::new(4),
        point: InternedStr::new(5),
        x: InternedStr::new(6),
        y: InternedStr::new(7),
        value: InternedStr::new(8),
        items: InternedStr::new(9),
        item: InternedStr::new(10),
        foo: InternedStr::new(11),
        bar: InternedStr::new(12),
        add: InternedStr::new(13),
        stage: InternedStr::new(14),
        compensate: InternedStr::new(15),
        text: InternedStr::new(16),
    }
}

fn assign_expr_type(table: &mut TypeTable, expr: &'static Expr<'static>, ty: TypeId) {
    table.insert(expr.id(), ty);
}

#[test]
fn lowers_function_instructions_and_structs() {
    let syms = symbols();
    let mut ast = AstFactory::new();
    let mut types = TypeArena::new();
    let point_ty = types.reserve_struct(syms.point);
    types.set_struct_fields(
        point_ty,
        vec![
            thagore_typeck::StructField {
                name: syms.x,
                ty: types.i32(),
            },
            thagore_typeck::StructField {
                name: syms.y,
                ty: types.i32(),
            },
        ],
    );

    let struct_decl = Decl::Struct(StructDecl {
        id: ast.id(),
        span: span(),
        name: syms.point,
        fields: leak_slice(vec![
            FieldDef {
                id: ast.id(),
                span: span(),
                name: syms.x,
                ty: named_type(&mut ast, syms.i32_),
            },
            FieldDef {
                id: ast.id(),
                span: span(),
                name: syms.y,
                ty: named_type(&mut ast, syms.i32_),
            },
        ]),
    });

    let sum_expr = {
        let lhs = ast.ident(syms.x);
        let rhs = ast.int(1);
        ast.binary(lhs, AstBinOp::Add, rhs)
    };
    let neg_expr = {
        let rhs = ast.ident(syms.x);
        ast.unary(AstUnaryOp::Neg, rhs)
    };
    let field_expr = {
        let value = ast.ident(syms.value);
        ast.field(value, syms.x)
    };
    let assign_field_expr = {
        let value = ast.ident(syms.value);
        let field = ast.field(value, syms.y);
        let rhs = ast.int(2);
        ast.assign(field, rhs)
    };
    let call_expr = {
        let callee = ast.ident(syms.add);
        let arg = ast.int(3);
        ast.call(callee, vec![arg])
    };
    let index_expr = {
        let items = ast.ident(syms.items);
        let idx = ast.int(0);
        ast.index(items, idx)
    };
    let ret_expr = ast.ident(syms.x);

    let text_init = ast.str_lit(syms.text);
    let text_stmt = Stmt::Let(LetDecl {
        id: ast.id(),
        span: span(),
        name: syms.text,
        ty: None,
        initializer: text_init,
    });
    let sum_stmt = expr_stmt(&mut ast, sum_expr);
    let neg_stmt = expr_stmt(&mut ast, neg_expr);
    let field_stmt = expr_stmt(&mut ast, field_expr);
    let assign_stmt = expr_stmt(&mut ast, assign_field_expr);
    let call_stmt = expr_stmt(&mut ast, call_expr);
    let index_stmt = expr_stmt(&mut ast, index_expr);
    let ret_stmt = return_stmt(&mut ast, Some(ret_expr));
    let body = ast.block(vec![
        text_stmt,
        sum_stmt,
        neg_stmt,
        field_stmt,
        assign_stmt,
        call_stmt,
        index_stmt,
        ret_stmt,
    ]);
    let param_x_ty = named_type(&mut ast, syms.i32_);
    let param_value_ty = named_type(&mut ast, syms.point);
    let array_item_ty = named_type(&mut ast, syms.i32_);
    let param_items_ty = leak_value(TypeExpr::Generic(thagore_ast::GenericTypeExpr {
        id: ast.id(),
        span: span(),
        name: syms.array_,
        args: leak_slice(vec![array_item_ty]),
    }));
    let return_ty = named_type(&mut ast, syms.i32_);
    let func_decl = Decl::Func(FuncDecl {
        id: ast.id(),
        span: span(),
        name: syms.foo,
        params: leak_slice(vec![
            Param {
                id: ast.id(),
                span: span(),
                name: syms.x,
                ty: param_x_ty,
            },
            Param {
                id: ast.id(),
                span: span(),
                name: syms.value,
                ty: param_value_ty,
            },
            Param {
                id: ast.id(),
                span: span(),
                name: syms.items,
                ty: param_items_ty,
            },
        ]),
        return_type: Some(return_ty),
        body,
    });

    let mut table = TypeTable::new();
    table.insert(struct_decl.id(), point_ty);
    table.insert(func_decl.id(), types.i32());
    if let Decl::Struct(node) = &struct_decl {
        for field in node.fields {
            table.insert(field.id, types.i32());
            table.insert(field.ty.id(), types.i32());
        }
    }
    if let Decl::Func(node) = &func_decl {
        for param in node.params {
            let ty = if param.name == syms.x {
                types.i32()
            } else if param.name == syms.value {
                point_ty
            } else {
                let array_ty = types.intern_array(types.i32());
                table.insert(param.ty.id(), array_ty);
                array_ty
            };
            table.insert(param.id, ty);
        }
        table.insert(param_x_ty.id(), types.i32());
        table.insert(param_value_ty.id(), point_ty);
        let array_ty = types.intern_array(types.i32());
        table.insert(array_item_ty.id(), types.i32());
        table.insert(param_items_ty.id(), array_ty);
        table.insert(return_ty.id(), types.i32());
        table.insert(node.return_type.expect("ret").id(), types.i32());
        if let Stmt::Let(let_decl) = &node.body.statements[0] {
            table.insert(let_decl.id, types.str());
            table.insert(let_decl.initializer.id(), types.str());
        }
    }
    for expr in [
        sum_expr, neg_expr, field_expr, call_expr, index_expr, ret_expr,
    ] {
        let ty = match expr {
            _ if expr.id() == sum_expr.id() => types.i32(),
            _ if expr.id() == neg_expr.id() => types.i32(),
            _ if expr.id() == field_expr.id() => types.i32(),
            _ if expr.id() == call_expr.id() => types.i32(),
            _ if expr.id() == index_expr.id() => types.i32(),
            _ => types.i32(),
        };
        assign_expr_type(&mut table, expr, ty);
    }
    if let Expr::Assign(assign) = assign_field_expr {
        table.insert(assign.id, types.i32());
        table.insert(assign.target.id(), types.i32());
        table.insert(assign.value.id(), types.i32());
        if let Expr::FieldAccess(field) = assign.target {
            table.insert(field.object.id(), point_ty);
        }
    }
    assign_expr_type(&mut table, assign_field_expr, types.i32());
    if let Expr::Binary(binary) = sum_expr {
        table.insert(binary.left.id(), types.i32());
        table.insert(binary.right.id(), types.i32());
    }
    if let Expr::Unary(unary) = neg_expr {
        table.insert(unary.operand.id(), types.i32());
    }
    if let Expr::FieldAccess(field) = field_expr {
        table.insert(field.object.id(), point_ty);
    }
    if let Expr::Assign(assign) = assign_field_expr {
        if let Expr::FieldAccess(field) = assign.target {
            table.insert(field.object.id(), point_ty);
        }
    }
    if let Expr::Call(call) = call_expr {
        table.insert(call.callee.id(), types.i32());
        table.insert(call.args[0].id(), types.i32());
    }
    if let Expr::Index(index) = index_expr {
        let array_ty = types.intern_array(types.i32());
        table.insert(index.object.id(), array_ty);
        table.insert(index.index.id(), types.i32());
    }
    table.insert(ret_expr.id(), types.i32());

    let mut lowerer = IrLowerer::new(syms.module, &types, &table);
    let module = lowerer
        .lower_module(&[struct_decl, func_decl])
        .expect("lowering should succeed");
    assert_eq!(module.structs.len(), 1);
    assert_eq!(module.functions.len(), 1);
    let function = &module.functions[0];
    let instructions: Vec<&Instr> = function
        .blocks
        .iter()
        .flat_map(|block| block.instructions.iter())
        .collect();
    assert!(instructions
        .iter()
        .any(|instr| matches!(instr, Instr::Alloc(..))));
    assert!(instructions
        .iter()
        .any(|instr| matches!(instr, Instr::Store(..))));
    assert!(instructions
        .iter()
        .any(|instr| matches!(instr, Instr::Load(..))));
    assert!(instructions
        .iter()
        .any(|instr| matches!(instr, Instr::Const(_, Const::Str(_)))));
    assert!(instructions
        .iter()
        .any(|instr| matches!(instr, Instr::BinOp(_, BinOp::Add, ..))));
    assert!(instructions
        .iter()
        .any(|instr| matches!(instr, Instr::UnOp(_, UnOp::Neg, _))));
    assert!(instructions
        .iter()
        .any(|instr| matches!(instr, Instr::GetField(..))));
    assert!(instructions
        .iter()
        .any(|instr| matches!(instr, Instr::SetField(..))));
    assert!(instructions
        .iter()
        .any(|instr| matches!(instr, Instr::Call(..))));
    assert!(instructions
        .iter()
        .any(|instr| matches!(instr, Instr::Index(..))));
}

#[test]
fn lowers_if_while_and_for_control_flow() {
    let syms = symbols();
    let mut ast = AstFactory::new();
    let mut types = TypeArena::new();
    let array_ty = types.intern_array(types.i32());

    let then_value = ast.int(1);
    let then_stmt = expr_stmt(&mut ast, then_value);
    let then_block = ast.block(vec![then_stmt]);
    let else_value = ast.int(2);
    let else_stmt = expr_stmt(&mut ast, else_value);
    let else_block = ast.block(vec![else_stmt]);
    let if_condition = ast.bool_lit(true);
    let if_stmt = Stmt::If(IfStmt {
        id: ast.id(),
        span: span(),
        condition: if_condition,
        then_block,
        else_block: Some(else_block),
    });
    let while_value = ast.int(3);
    let while_stmt_expr = expr_stmt(&mut ast, while_value);
    let while_body = ast.block(vec![while_stmt_expr]);
    let while_condition = ast.bool_lit(true);
    let while_stmt = Stmt::While(WhileStmt {
        id: ast.id(),
        span: span(),
        condition: while_condition,
        body: while_body,
    });
    let item_expr = ast.ident(syms.item);
    let for_body_stmt = expr_stmt(&mut ast, item_expr);
    let for_body = ast.block(vec![for_body_stmt]);
    let items_expr = ast.ident(syms.items);
    let for_stmt = Stmt::For(ForStmt {
        id: ast.id(),
        span: span(),
        binding: syms.item,
        iterator: items_expr,
        body: for_body,
    });
    let ret_stmt = return_stmt(&mut ast, None);
    let body = ast.block(vec![if_stmt, while_stmt, for_stmt, ret_stmt]);
    let items_ty_expr = named_type(&mut ast, syms.array_);
    let func_decl = Decl::Func(FuncDecl {
        id: ast.id(),
        span: span(),
        name: syms.foo,
        params: leak_slice(vec![Param {
            id: ast.id(),
            span: span(),
            name: syms.items,
            ty: items_ty_expr,
        }]),
        return_type: None,
        body,
    });

    let mut table = TypeTable::new();
    if let Decl::Func(node) = &func_decl {
        table.insert(node.id, types.unit());
        table.insert(node.params[0].id, array_ty);
        table.insert(items_ty_expr.id(), array_ty);
        table.insert(node.body.id, types.unit());
        for stmt in node.body.statements {
            table.insert(stmt.id(), types.unit());
            match stmt {
                Stmt::If(node) => {
                    table.insert(node.condition.id(), types.bool());
                    table.insert(node.then_block.id, types.unit());
                    table.insert(node.else_block.expect("else").id, types.unit());
                    if let Stmt::Expr(expr) = &node.then_block.statements[0] {
                        table.insert(expr.id, types.unit());
                        table.insert(expr.expr.id(), types.i32());
                    }
                    if let Some(else_block) = node.else_block {
                        if let Stmt::Expr(expr) = &else_block.statements[0] {
                            table.insert(expr.id, types.unit());
                            table.insert(expr.expr.id(), types.i32());
                        }
                    }
                }
                Stmt::While(node) => {
                    table.insert(node.condition.id(), types.bool());
                    table.insert(node.body.id, types.unit());
                    if let Stmt::Expr(expr) = &node.body.statements[0] {
                        table.insert(expr.id, types.unit());
                        table.insert(expr.expr.id(), types.i32());
                    }
                }
                Stmt::For(node) => {
                    table.insert(node.iterator.id(), array_ty);
                    table.insert(node.body.id, types.unit());
                    if let Stmt::Expr(expr) = &node.body.statements[0] {
                        table.insert(expr.id, types.unit());
                        table.insert(expr.expr.id(), types.i32());
                    }
                }
                Stmt::Return(node) => table.insert(node.id, types.unit()),
                Stmt::Expr(node) => table.insert(node.expr.id(), types.i32()),
                Stmt::Let(_) => {}
            }
        }
    }

    let mut lowerer = IrLowerer::new(syms.module, &types, &table);
    let module = lowerer
        .lower_module(&[func_decl])
        .expect("lowering should succeed");
    let function = &module.functions[0];
    assert!(function
        .blocks
        .iter()
        .any(|block| { matches!(block.terminator, Some(Terminator::Branch(_, _, _))) }));
    assert!(function.blocks.iter().any(|block| {
        block
            .instructions
            .iter()
            .any(|instr| matches!(instr, Instr::Phi(..)))
    }));
    assert!(function.blocks.iter().any(|block| {
        block
            .instructions
            .iter()
            .any(|instr| matches!(instr, Instr::Call(_, name, _) if *name == ARRAY_LEN_INTRINSIC))
    }));
}

#[test]
fn lowers_intent_and_flow_blocks() {
    let syms = symbols();
    let mut ast = AstFactory::new();
    let types = TypeArena::new();
    let constraint = ast.bool_lit(true);
    let intent_ret = return_stmt(&mut ast, None);
    let intent_body = ast.block(vec![intent_ret]);
    let intent = Decl::Intent(thagore_ast::IntentDecl {
        id: ast.id(),
        span: span(),
        name: syms.foo,
        constraints: leak_slice(vec![constraint]),
        body: intent_body,
    });
    let stage_one_expr = ast.int(1);
    let stage_one_stmt = expr_stmt(&mut ast, stage_one_expr);
    let stage_one_body = ast.block(vec![stage_one_stmt]);
    let stage_two_expr = ast.int(2);
    let stage_two_stmt = expr_stmt(&mut ast, stage_two_expr);
    let stage_two_body = ast.block(vec![stage_two_stmt]);
    let compensation_expr = ast.int(3);
    let compensation_stmt = expr_stmt(&mut ast, compensation_expr);
    let compensation_body = ast.block(vec![compensation_stmt]);
    let flow = Decl::Flow(FlowDecl {
        id: ast.id(),
        span: span(),
        name: syms.bar,
        stages: leak_slice(vec![
            FlowStage {
                id: ast.id(),
                span: span(),
                name: syms.stage,
                body: stage_one_body,
            },
            FlowStage {
                id: ast.id(),
                span: span(),
                name: syms.compensate,
                body: stage_two_body,
            },
        ]),
        compensation: Some(compensation_body),
    });
    let mut table = TypeTable::new();
    for decl in [&intent, &flow] {
        table.insert(decl.id(), types.unit());
    }
    table.insert(constraint.id(), types.bool());
    table.insert(intent_body.id, types.unit());
    if let Decl::Flow(flow_decl) = &flow {
        table.insert(flow_decl.stages[0].body.id, types.unit());
        table.insert(flow_decl.stages[1].body.id, types.unit());
        table.insert(flow_decl.compensation.expect("comp").id, types.unit());
        if let Stmt::Expr(expr) = &flow_decl.stages[0].body.statements[0] {
            table.insert(expr.id, types.unit());
            table.insert(expr.expr.id(), types.i32());
        }
        if let Stmt::Expr(expr) = &flow_decl.stages[1].body.statements[0] {
            table.insert(expr.id, types.unit());
            table.insert(expr.expr.id(), types.i32());
        }
        if let Stmt::Expr(expr) = &flow_decl.compensation.expect("comp").statements[0] {
            table.insert(expr.id, types.unit());
            table.insert(expr.expr.id(), types.i32());
        }
    }

    let mut lowerer = IrLowerer::new(syms.module, &types, &table);
    let module = lowerer
        .lower_module(&[intent, flow])
        .expect("lowering should succeed");
    assert_eq!(module.functions.len(), 3);
    assert!(module
        .functions
        .iter()
        .any(|function| function.name == syms.foo));
    assert!(module
        .functions
        .iter()
        .any(|function| function.name == syms.bar));
}

#[test]
fn validation_catches_malformed_ir() {
    let mut function = IrFunction::new(InternedStr::new(1), TypeId::new(0));
    function.entry = BlockId::new(0);
    function.value_types.push(TypeId::new(0));
    function.blocks.push(BasicBlock {
        id: BlockId::new(0),
        instructions: vec![Instr::Load(Value::new(0), Value::new(9))],
        terminator: None,
        successors: vec![BlockId::new(3)],
        predecessors: vec![],
    });
    let errors = validate_function(&function);
    assert!(errors
        .iter()
        .any(|error| matches!(error, thagore_ir::LoweringError::MissingTerminator { .. })));
    assert!(errors
        .iter()
        .any(|error| matches!(error, thagore_ir::LoweringError::UndefinedValue { .. })));
    assert!(errors
        .iter()
        .any(|error| matches!(error, thagore_ir::LoweringError::InvalidBlockEdge { .. })));
}

#[test]
fn validate_module_accepts_well_formed_ir() {
    let mut function = IrFunction::new(InternedStr::new(1), TypeId::new(0));
    function.entry = BlockId::new(0);
    function.value_types.push(TypeId::new(0));
    function.blocks.push(BasicBlock {
        id: BlockId::new(0),
        instructions: vec![Instr::Const(Value::new(0), Const::Unit)],
        terminator: Some(Terminator::Return(Some(Value::new(0)))),
        successors: vec![],
        predecessors: vec![],
    });
    let module = thagore_ir::IrModule {
        name: InternedStr::new(1),
        functions: vec![function],
        structs: vec![],
    };
    assert!(validate_module(&module).is_empty());
}
