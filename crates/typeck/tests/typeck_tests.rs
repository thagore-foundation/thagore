use thagore_ast::{
    BinOp, BinaryExpr, Block, BreakStmt, CallExpr, ConstDecl, Constraint, ConstraintKind, ContinueStmt, Decl, Expr,
    ExprStmt, FieldAccessExpr, FieldDef, FlowDecl, FlowStage, ForStmt, FuncDecl, GenericFuncDecl,
    GenericImplBlock, GenericStructDecl, GenericTypeExpr, IdentExpr, IfStmt, ImplBlock, IndexExpr,
    InferTypeExpr, InternedStr, LetDecl, LitExpr, Literal, NamedTypeExpr, NodeId, Param,
    ReturnStmt, Span, Stmt, StructDecl, TypeExpr, TypeParam, UnaryExpr, UnaryOp, WhileStmt,
};
use thagore_typeck::{
    InferenceSolver, ScopeStack, TypeArena, TypeChecker, TypeConstraint, TypeError, TypeId,
    TypeTable,
};

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

    fn alloc_type(&self, ty: TypeExpr<'static>) -> &'static TypeExpr<'static> {
        leak_value(ty)
    }

    fn alloc_expr(&self, expr: Expr<'static>) -> &'static Expr<'static> {
        leak_value(expr)
    }

    fn alloc_block(&self, block: Block<'static>) -> &'static Block<'static> {
        leak_value(block)
    }

    fn named_type(&mut self, name: InternedStr) -> &'static TypeExpr<'static> {
        let id = self.id();
        self.alloc_type(TypeExpr::Named(NamedTypeExpr {
            id,
            span: span(),
            name,
        }))
    }

    fn infer_type(&mut self) -> &'static TypeExpr<'static> {
        let id = self.id();
        self.alloc_type(TypeExpr::Infer(InferTypeExpr { id, span: span() }))
    }

    fn array_type(
        &mut self,
        array_name: InternedStr,
        element: &'static TypeExpr<'static>,
    ) -> &'static TypeExpr<'static> {
        let id = self.id();
        self.alloc_type(TypeExpr::Generic(GenericTypeExpr {
            id,
            span: span(),
            name: array_name,
            args: leak_slice(vec![element]),
        }))
    }

    fn ident(&mut self, name: InternedStr) -> &'static Expr<'static> {
        let id = self.id();
        self.alloc_expr(Expr::Ident(IdentExpr {
            id,
            span: span(),
            name,
        }))
    }

    fn int(&mut self, value: i64) -> &'static Expr<'static> {
        let id = self.id();
        self.alloc_expr(Expr::Literal(LitExpr {
            id,
            span: span(),
            literal: Literal::Int(value),
        }))
    }

    fn bool_lit(&mut self, value: bool) -> &'static Expr<'static> {
        let id = self.id();
        self.alloc_expr(Expr::Literal(LitExpr {
            id,
            span: span(),
            literal: Literal::Bool(value),
        }))
    }

    fn float(&mut self, value: f64) -> &'static Expr<'static> {
        let id = self.id();
        self.alloc_expr(Expr::Literal(LitExpr {
            id,
            span: span(),
            literal: Literal::Float(value),
        }))
    }

    fn str_lit(&mut self, value: InternedStr) -> &'static Expr<'static> {
        let id = self.id();
        self.alloc_expr(Expr::Literal(LitExpr {
            id,
            span: span(),
            literal: Literal::Str(value),
        }))
    }

    fn binary(
        &mut self,
        left: &'static Expr<'static>,
        op: BinOp,
        right: &'static Expr<'static>,
    ) -> &'static Expr<'static> {
        let id = self.id();
        self.alloc_expr(Expr::Binary(BinaryExpr {
            id,
            span: span(),
            left,
            op,
            right,
        }))
    }

    fn unary(&mut self, op: UnaryOp, operand: &'static Expr<'static>) -> &'static Expr<'static> {
        let id = self.id();
        self.alloc_expr(Expr::Unary(UnaryExpr {
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
        self.alloc_expr(Expr::Call(CallExpr {
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
        self.alloc_expr(Expr::FieldAccess(FieldAccessExpr {
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
        self.alloc_expr(Expr::Index(IndexExpr {
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
        self.alloc_expr(Expr::Assign(thagore_ast::AssignExpr {
            id,
            span: span(),
            target,
            value,
        }))
    }

    fn block(&mut self, statements: Vec<Stmt<'static>>) -> &'static Block<'static> {
        let id = self.id();
        self.alloc_block(Block {
            id,
            span: span(),
            statements: leak_slice(statements),
        })
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

fn let_stmt(
    ast: &mut AstFactory,
    name: InternedStr,
    ty: Option<&'static TypeExpr<'static>>,
    initializer: &'static Expr<'static>,
) -> Stmt<'static> {
    Stmt::Let(LetDecl {
        id: ast.id(),
        span: span(),
        name,
        ty,
        initializer,
    })
}

#[derive(Clone, Copy)]
struct Symbols {
    i32_: InternedStr,
    f64_: InternedStr,
    bool_: InternedStr,
    str_: InternedStr,
    array_: InternedStr,
    point: InternedStr,
    x: InternedStr,
    y: InternedStr,
    value: InternedStr,
    flag: InternedStr,
    items: InternedStr,
    item: InternedStr,
    foo: InternedStr,
    bar: InternedStr,
    add: InternedStr,
    take: InternedStr,
    missing: InternedStr,
    distance: InternedStr,
    text: InternedStr,
    optimize: InternedStr,
    stage: InternedStr,
    temp: InternedStr,
}

fn symbols() -> Symbols {
    Symbols {
        i32_: InternedStr::new(1),
        f64_: InternedStr::new(2),
        bool_: InternedStr::new(3),
        str_: InternedStr::new(4),
        array_: InternedStr::new(5),
        point: InternedStr::new(6),
        x: InternedStr::new(7),
        y: InternedStr::new(8),
        value: InternedStr::new(9),
        flag: InternedStr::new(10),
        items: InternedStr::new(11),
        item: InternedStr::new(12),
        foo: InternedStr::new(13),
        bar: InternedStr::new(14),
        add: InternedStr::new(15),
        take: InternedStr::new(16),
        missing: InternedStr::new(17),
        distance: InternedStr::new(18),
        text: InternedStr::new(19),
        optimize: InternedStr::new(20),
        stage: InternedStr::new(21),
        temp: InternedStr::new(22),
    }
}

fn checker_with_symbols() -> TypeChecker {
    let mut checker = TypeChecker::new();
    let syms = symbols();
    checker.register_symbol_name(syms.i32_, "i32");
    checker.register_symbol_name(syms.f64_, "f64");
    checker.register_symbol_name(syms.bool_, "bool");
    checker.register_symbol_name(syms.str_, "str");
    checker.register_symbol_name(syms.array_, "Array");
    checker.register_symbol_name(syms.point, "Point");
    checker.register_symbol_name(syms.x, "x");
    checker.register_symbol_name(syms.y, "y");
    checker.register_symbol_name(syms.value, "value");
    checker.register_symbol_name(syms.flag, "flag");
    checker.register_symbol_name(syms.items, "items");
    checker.register_symbol_name(syms.item, "item");
    checker.register_symbol_name(syms.foo, "foo");
    checker.register_symbol_name(syms.bar, "bar");
    checker.register_symbol_name(syms.add, "add");
    checker.register_symbol_name(syms.take, "take");
    checker.register_symbol_name(syms.missing, "missing");
    checker.register_symbol_name(syms.distance, "distance");
    checker.register_symbol_name(syms.text, "text");
    checker.register_symbol_name(syms.optimize, "Optimize");
    checker.register_symbol_name(syms.stage, "stage");
    checker.register_symbol_name(syms.temp, "temp");
    checker
}

#[test]
fn type_arena_interns_and_exposes_builtins() {
    let mut arena = TypeArena::new();
    let array_a = arena.intern_array(arena.i32());
    let array_b = arena.intern_array(arena.i32());
    assert_eq!(array_a, array_b);
    assert!(arena.is_numeric(arena.i32()));
    assert!(!arena.is_numeric(arena.bool()));
}

#[test]
fn scope_stack_push_pop_and_lookup() {
    let mut scopes = ScopeStack::new();
    let key = InternedStr::new(1);
    scopes.insert(key, TypeId::new(7));
    assert_eq!(scopes.get(&key), Some(&TypeId::new(7)));
    scopes.push_scope();
    scopes.insert(key, TypeId::new(9));
    assert_eq!(scopes.get(&key), Some(&TypeId::new(9)));
    scopes.pop_scope();
    assert_eq!(scopes.get(&key), Some(&TypeId::new(7)));
}

#[test]
fn type_table_records_node_types() {
    let mut table = TypeTable::new();
    let node = NodeId::new(3);
    let ty = TypeId::new(11);
    table.insert(node, ty);
    assert_eq!(table.get(node), Some(ty));
}

#[test]
fn inference_solver_unifies_inference_variables() {
    let mut arena = TypeArena::new();
    let infer = arena.fresh_infer();
    let mut solver = InferenceSolver::new();
    let mut errors = Vec::new();
    let resolved = solver.add_equality(infer, arena.i32(), span(), &arena, &mut errors);
    assert!(errors.is_empty());
    assert_eq!(solver.resolve(resolved), solver.resolve(arena.i32()));
    assert!(matches!(
        solver.constraints()[0],
        TypeConstraint::Equal { .. }
    ));
}

#[test]
fn infers_let_types_and_collects_forward_declarations() {
    let syms = symbols();
    let mut ast = AstFactory::new();

    let point_field_ty = ast.named_type(syms.i32_);
    let point_struct = Decl::Struct(StructDecl {
        id: ast.id(),
        span: span(),
        name: syms.point,
        fields: leak_slice(vec![FieldDef {
            id: ast.id(),
            span: span(),
            name: syms.x,
            ty: point_field_ty,
        }]),
    });

    let let_init = ast.int(42);
    let let_decl = LetDecl {
        id: ast.id(),
        span: span(),
        name: syms.value,
        ty: None,
        initializer: let_init,
    };

    let param_ty = ast.named_type(syms.point);
    let return_ty = ast.named_type(syms.point);
    let return_expr = ast.ident(syms.x);
    let ret_stmt = return_stmt(&mut ast, Some(return_expr));
    let body = ast.block(vec![ret_stmt]);
    let function = Decl::Func(FuncDecl {
        id: ast.id(),
        span: span(),
        name: syms.take,
        params: leak_slice(vec![Param {
            id: ast.id(),
            span: span(),
            name: syms.x,
            ty: param_ty,
        }]),
        return_type: Some(return_ty),
        body,
    });

    let mut checker = checker_with_symbols();
    let errors = checker
        .check(&[function, point_struct, Decl::Let(let_decl.clone())])
        .expect_err("top-level let should now fail early");
    assert!(errors.iter().any(|error| matches!(
        error,
        TypeError::UnsupportedFeature { feature, .. } if *feature == "top-level let declarations"
    )));
}

#[test]
fn reports_type_mismatch_unknown_identifier_and_inference_failure() {
    let syms = symbols();
    let mut ast = AstFactory::new();
    let inferred = ast.infer_type();
    let missing = ast.ident(syms.missing);
    let unknown_decl = Decl::Let(LetDecl {
        id: ast.id(),
        span: span(),
        name: syms.value,
        ty: Some(inferred),
        initializer: missing,
    });

    let mut checker = checker_with_symbols();
    let errors = checker.check(&[unknown_decl]).expect_err("expected errors");
    assert!(errors
        .iter()
        .any(|error| matches!(error, TypeError::UnknownIdentifier { .. })));
    assert!(errors
        .iter()
        .any(|error| matches!(error, TypeError::InferenceFailure { .. })));

    let mut ast = AstFactory::new();
    let bool_ty = ast.named_type(syms.bool_);
    let one = ast.int(1);
    let mismatch_decl = Decl::Let(LetDecl {
        id: ast.id(),
        span: span(),
        name: syms.value,
        ty: Some(bool_ty),
        initializer: one,
    });

    let mut checker = checker_with_symbols();
    let errors = checker
        .check(&[mismatch_decl])
        .expect_err("expected mismatch");
    assert!(errors
        .iter()
        .any(|error| matches!(error, TypeError::TypeMismatch { .. })));
}

#[test]
fn reports_unknown_field_and_not_indexable() {
    let syms = symbols();
    let mut ast = AstFactory::new();

    let point_field_ty = ast.named_type(syms.i32_);
    let point_struct = Decl::Struct(StructDecl {
        id: ast.id(),
        span: span(),
        name: syms.point,
        fields: leak_slice(vec![FieldDef {
            id: ast.id(),
            span: span(),
            name: syms.x,
            ty: point_field_ty,
        }]),
    });

    let param_ty = ast.named_type(syms.point);
    let return_ty = ast.named_type(syms.i32_);
    let unknown_field_expr = {
        let value = ast.ident(syms.value);
        ast.field(value, syms.y)
    };
    let bad_index_expr = {
        let value = ast.ident(syms.value);
        let zero = ast.int(0);
        ast.index(value, zero)
    };
    let field_stmt = expr_stmt(&mut ast, unknown_field_expr);
    let index_stmt = return_stmt(&mut ast, Some(bad_index_expr));
    let body = ast.block(vec![field_stmt, index_stmt]);
    let function = Decl::Func(FuncDecl {
        id: ast.id(),
        span: span(),
        name: syms.foo,
        params: leak_slice(vec![Param {
            id: ast.id(),
            span: span(),
            name: syms.value,
            ty: param_ty,
        }]),
        return_type: Some(return_ty),
        body,
    });

    let mut checker = checker_with_symbols();
    let errors = checker
        .check(&[point_struct, function])
        .expect_err("expected field and index errors");
    assert!(errors
        .iter()
        .any(|error| matches!(error, TypeError::UnknownField { .. })));
    assert!(errors
        .iter()
        .any(|error| matches!(error, TypeError::NotIndexable { .. })));
}

#[test]
fn reports_call_errors_and_return_type_mismatch() {
    let syms = symbols();
    let mut ast = AstFactory::new();

    let x_ty = ast.named_type(syms.i32_);
    let y_ty = ast.named_type(syms.i32_);
    let add_return_ty = ast.named_type(syms.i32_);
    let sum_expr = {
        let lhs = ast.ident(syms.x);
        let rhs = ast.ident(syms.y);
        ast.binary(lhs, BinOp::Add, rhs)
    };
    let add_return = return_stmt(&mut ast, Some(sum_expr));
    let add_body = ast.block(vec![add_return]);
    let callee = Decl::Func(FuncDecl {
        id: ast.id(),
        span: span(),
        name: syms.add,
        params: leak_slice(vec![
            Param {
                id: ast.id(),
                span: span(),
                name: syms.x,
                ty: x_ty,
            },
            Param {
                id: ast.id(),
                span: span(),
                name: syms.y,
                ty: y_ty,
            },
        ]),
        return_type: Some(add_return_ty),
        body: add_body,
    });

    let caller_return_ty = ast.named_type(syms.bool_);
    let bad_arity_call = {
        let callee_expr = ast.ident(syms.add);
        let arg = ast.int(1);
        ast.call(callee_expr, vec![arg])
    };
    let not_callable_call = {
        let callee_expr = ast.int(2);
        ast.call(callee_expr, vec![])
    };
    let bad_return_value = ast.int(1);
    let bad_arity_stmt = expr_stmt(&mut ast, bad_arity_call);
    let not_callable_stmt = expr_stmt(&mut ast, not_callable_call);
    let bad_return_stmt = return_stmt(&mut ast, Some(bad_return_value));
    let caller_body = ast.block(vec![bad_arity_stmt, not_callable_stmt, bad_return_stmt]);
    let caller = Decl::Func(FuncDecl {
        id: ast.id(),
        span: span(),
        name: syms.foo,
        params: leak_slice(vec![]),
        return_type: Some(caller_return_ty),
        body: caller_body,
    });

    let mut checker = checker_with_symbols();
    let errors = checker
        .check(&[callee, caller])
        .expect_err("expected call and return errors");
    assert!(errors
        .iter()
        .any(|error| matches!(error, TypeError::ArgumentCountMismatch { .. })));
    assert!(errors
        .iter()
        .any(|error| matches!(error, TypeError::NotCallable { .. })));
    assert!(errors
        .iter()
        .any(|error| matches!(error, TypeError::ReturnTypeMismatch { .. })));
}

#[test]
fn checks_conditions_and_for_loop_binding() {
    let syms = symbols();
    let mut ast = AstFactory::new();

    let flag_ty = ast.named_type(syms.bool_);
    let item_ty = ast.named_type(syms.i32_);
    let items_ty = ast.array_type(syms.array_, item_ty);
    let return_ty = ast.named_type(syms.i32_);

    let then_block = {
        let zero = ast.int(0);
        let stmt = return_stmt(&mut ast, Some(zero));
        ast.block(vec![stmt])
    };
    let for_body = {
        let item = ast.ident(syms.item);
        let stmt = return_stmt(&mut ast, Some(item));
        ast.block(vec![stmt])
    };
    let while_body = {
        let items = ast.ident(syms.items);
        let for_stmt = Stmt::For(ForStmt {
            id: ast.id(),
            span: span(),
            binding: syms.item,
            iterator: items,
            body: for_body,
        });
        ast.block(vec![for_stmt])
    };
    let else_block = {
        let bad_while_cond = ast.int(2);
        let while_stmt = Stmt::While(WhileStmt {
            id: ast.id(),
            span: span(),
            condition: bad_while_cond,
            body: while_body,
        });
        ast.block(vec![while_stmt])
    };
    let iter_body = {
        let bad_if_cond = ast.int(1);
        let if_stmt = Stmt::If(IfStmt {
            id: ast.id(),
            span: span(),
            condition: bad_if_cond,
            then_block,
            else_block: Some(else_block),
        });
        ast.block(vec![if_stmt])
    };
    let iter_func = Decl::Func(FuncDecl {
        id: ast.id(),
        span: span(),
        name: syms.take,
        params: leak_slice(vec![
            Param {
                id: ast.id(),
                span: span(),
                name: syms.flag,
                ty: flag_ty,
            },
            Param {
                id: ast.id(),
                span: span(),
                name: syms.items,
                ty: items_ty,
            },
        ]),
        return_type: Some(return_ty),
        body: iter_body,
    });

    let mut checker = checker_with_symbols();
    let errors = checker
        .check(&[iter_func])
        .expect_err("expected condition errors");
    assert_eq!(
        errors
            .iter()
            .filter(|error| matches!(error, TypeError::ConditionNotBool { .. }))
            .count(),
        2
    );

    let mut ast = AstFactory::new();
    let item_ty = ast.named_type(syms.i32_);
    let items_ty = ast.array_type(syms.array_, item_ty);
    let return_ty = ast.named_type(syms.i32_);
    let ok_for_body = {
        let item = ast.ident(syms.item);
        let stmt = return_stmt(&mut ast, Some(item));
        ast.block(vec![stmt])
    };
    let ok_body = {
        let items = ast.ident(syms.items);
        let for_stmt = Stmt::For(ForStmt {
            id: ast.id(),
            span: span(),
            binding: syms.item,
            iterator: items,
            body: ok_for_body,
        });
        ast.block(vec![for_stmt])
    };
    let ok_func = Decl::Func(FuncDecl {
        id: ast.id(),
        span: span(),
        name: syms.take,
        params: leak_slice(vec![Param {
            id: ast.id(),
            span: span(),
            name: syms.items,
            ty: items_ty,
        }]),
        return_type: Some(return_ty),
        body: ok_body,
    });

    let mut checker = checker_with_symbols();
    assert!(checker.check(&[ok_func]).is_ok());
}

#[test]
fn checks_field_access_assignment_and_intent_flow_scope_isolation() {
    let syms = symbols();
    let mut ast = AstFactory::new();

    let point_field_ty = ast.named_type(syms.i32_);
    let point_struct = Decl::Struct(StructDecl {
        id: ast.id(),
        span: span(),
        name: syms.point,
        fields: leak_slice(vec![FieldDef {
            id: ast.id(),
            span: span(),
            name: syms.x,
            ty: point_field_ty,
        }]),
    });

    let method_param_ty = ast.named_type(syms.point);
    let method_return_ty = ast.named_type(syms.i32_);
    let method_body = {
        let one = ast.int(1);
        let stmt = return_stmt(&mut ast, Some(one));
        ast.block(vec![stmt])
    };
    let method_impl = Decl::Impl(ImplBlock {
        id: ast.id(),
        span: span(),
        target: syms.point,
        methods: leak_slice(vec![FuncDecl {
            id: ast.id(),
            span: span(),
            name: syms.distance,
            params: leak_slice(vec![Param {
                id: ast.id(),
                span: span(),
                name: syms.value,
                ty: method_param_ty,
            }]),
            return_type: Some(method_return_ty),
            body: method_body,
        }]),
    });

    let func_param_ty = ast.named_type(syms.point);
    let func_return_ty = ast.named_type(syms.i32_);
    let assign_expr = {
        let value = ast.ident(syms.value);
        let field = ast.field(value, syms.x);
        let two = ast.int(2);
        ast.assign(field, two)
    };
    let method_call = {
        let value = ast.ident(syms.value);
        let method = ast.field(value, syms.distance);
        ast.call(method, vec![])
    };
    let assign_stmt = expr_stmt(&mut ast, assign_expr);
    let call_stmt = return_stmt(&mut ast, Some(method_call));
    let func_body = ast.block(vec![assign_stmt, call_stmt]);
    let func = Decl::Func(FuncDecl {
        id: ast.id(),
        span: span(),
        name: syms.foo,
        params: leak_slice(vec![Param {
            id: ast.id(),
            span: span(),
            name: syms.value,
            ty: func_param_ty,
        }]),
        return_type: Some(func_return_ty),
        body: func_body,
    });

    let constraint = ast.bool_lit(true);
    let intent_body = {
        let one = ast.int(1);
        let stmt = let_stmt(&mut ast, syms.temp, None, one);
        ast.block(vec![stmt])
    };
    let intent = Decl::Intent(thagore_ast::IntentDecl {
        id: ast.id(),
        span: span(),
        name: syms.optimize,
        constraints: leak_slice(vec![constraint]),
        body: intent_body,
    });

    let flow_stage_body = {
        let text = ast.str_lit(syms.text);
        let stmt = let_stmt(&mut ast, syms.text, None, text);
        ast.block(vec![stmt])
    };
    let flow = Decl::Flow(FlowDecl {
        id: ast.id(),
        span: span(),
        name: syms.bar,
        stages: leak_slice(vec![FlowStage {
            id: ast.id(),
            span: span(),
            name: syms.stage,
            body: flow_stage_body,
        }]),
        compensation: None,
    });

    let leaked_init = ast.ident(syms.temp);
    let leaked = Decl::Let(LetDecl {
        id: ast.id(),
        span: span(),
        name: syms.value,
        ty: None,
        initializer: leaked_init,
    });

    let mut checker = checker_with_symbols();
    let errors = checker
        .check(&[point_struct, method_impl, func, intent, flow, leaked])
        .expect_err("expected leaked scope error");
    assert!(errors.iter().any(
        |error| matches!(error, TypeError::UnknownIdentifier { name, .. } if *name == syms.temp)
    ));
}

#[test]
fn checks_unary_bool_and_numeric_rules() {
    let syms = symbols();
    let mut ast = AstFactory::new();

    let bool_ty = ast.named_type(syms.bool_);
    let return_ty = ast.named_type(syms.bool_);
    let not_expr = {
        let flag = ast.ident(syms.flag);
        ast.unary(UnaryOp::Not, flag)
    };
    let neg_expr = {
        let flag = ast.ident(syms.flag);
        ast.unary(UnaryOp::Neg, flag)
    };
    let neg_stmt = expr_stmt(&mut ast, neg_expr);
    let ret_stmt = return_stmt(&mut ast, Some(not_expr));
    let body = ast.block(vec![neg_stmt, ret_stmt]);
    let func = Decl::Func(FuncDecl {
        id: ast.id(),
        span: span(),
        name: syms.foo,
        params: leak_slice(vec![Param {
            id: ast.id(),
            span: span(),
            name: syms.flag,
            ty: bool_ty,
        }]),
        return_type: Some(return_ty),
        body,
    });

    let mut checker = checker_with_symbols();
    let errors = checker
        .check(&[func])
        .expect_err("expected numeric mismatch");
    assert!(errors
        .iter()
        .any(|error| matches!(error, TypeError::TypeMismatch { .. })));
}

#[test]
fn instantiates_generic_function_calls() {
    let syms = symbols();
    let mut ast = AstFactory::new();

    let generic_param = TypeParam {
        id: ast.id(),
        span: span(),
        name: syms.value,
        constraints: leak_slice(vec![Constraint {
            id: ast.id(),
            span: span(),
            kind: ConstraintKind::Numeric,
        }]),
    };
    let generic_ty = ast.named_type(syms.value);
    let generic_body = {
        let value = ast.ident(syms.value);
        let stmt = return_stmt(&mut ast, Some(value));
        ast.block(vec![stmt])
    };
    let generic_abs = Decl::GenericFunc(GenericFuncDecl {
        id: ast.id(),
        span: span(),
        name: syms.foo,
        type_params: leak_slice(vec![generic_param]),
        params: leak_slice(vec![Param {
            id: ast.id(),
            span: span(),
            name: syms.value,
            ty: generic_ty,
        }]),
        return_type: Some(generic_ty),
        body: generic_body,
    });

    let call = {
        let callee = ast.ident(syms.foo);
        let arg = ast.int(42);
        ast.call(callee, vec![arg])
    };
    let caller_return = return_stmt(&mut ast, Some(call));
    let caller_body = ast.block(vec![caller_return]);
    let caller = Decl::Func(FuncDecl {
        id: ast.id(),
        span: span(),
        name: syms.add,
        params: leak_slice(vec![]),
        return_type: Some(ast.named_type(syms.i32_)),
        body: caller_body,
    });

    let mut checker = checker_with_symbols();
    let table = checker
        .check(&[generic_abs, caller.clone()])
        .expect("generic call should type check");
    let Decl::Func(caller_decl) = caller else {
        panic!("expected caller function")
    };
    let Stmt::Return(ret) = &caller_decl.body.statements[0] else {
        panic!("expected return")
    };
    let call_expr = ret.value.expect("call value");
    assert_eq!(table.get(call_expr.id()), Some(checker.types().i32()));
    assert_eq!(checker.monomorphs().pending().len(), 1);
}

#[test]
fn binds_top_level_consts_into_scope() {
    let syms = symbols();
    let mut ast = AstFactory::new();

    let const_decl = Decl::Const(ConstDecl {
        id: ast.id(),
        span: span(),
        name: syms.value,
        type_ann: ast.named_type(syms.f64_),
        value: ast.float(3.14),
    });
    let ident = ast.ident(syms.value);
    let func_return = return_stmt(&mut ast, Some(ident));
    let func_body = ast.block(vec![func_return]);
    let func = Decl::Func(FuncDecl {
        id: ast.id(),
        span: span(),
        name: syms.take,
        params: leak_slice(vec![]),
        return_type: Some(ast.named_type(syms.f64_)),
        body: func_body,
    });

    let mut checker = checker_with_symbols();
    let table = checker
        .check(&[const_decl, func.clone()])
        .expect("const should type check");
    let Decl::Func(func_decl) = func else {
        panic!("expected function")
    };
    let Stmt::Return(ret) = &func_decl.body.statements[0] else {
        panic!("expected return")
    };
    let value = ret.value.expect("const value");
    assert_eq!(table.get(value.id()), Some(checker.types().f64()));
}

#[test]
fn reports_generic_struct_and_impl_as_unsupported() {
    let syms = symbols();
    let mut ast = AstFactory::new();

    let field_ty = ast.named_type(syms.i32_);
    let type_param = TypeParam {
        id: ast.id(),
        span: span(),
        name: syms.value,
        constraints: leak_slice(vec![]),
    };

    let generic_struct = Decl::GenericStruct(GenericStructDecl {
        id: ast.id(),
        span: span(),
        name: syms.point,
        type_params: leak_slice(vec![type_param.clone()]),
        fields: leak_slice(vec![FieldDef {
            id: ast.id(),
            span: span(),
            name: syms.x,
            ty: field_ty,
        }]),
    });

    let method_body = {
        let zero = ast.int(0);
        let stmt = return_stmt(&mut ast, Some(zero));
        ast.block(vec![stmt])
    };

    let generic_impl = Decl::GenericImpl(GenericImplBlock {
        id: ast.id(),
        span: span(),
        target: syms.point,
        type_params: leak_slice(vec![type_param]),
        methods: leak_slice(vec![FuncDecl {
            id: ast.id(),
            span: span(),
            name: syms.distance,
            params: leak_slice(vec![]),
            return_type: Some(ast.named_type(syms.i32_)),
            body: method_body,
        }]),
    });

    let mut checker = checker_with_symbols();
    let errors = checker
        .check(&[generic_struct, generic_impl])
        .expect_err("generic struct/impl should not type check");
    assert!(errors.iter().any(|error| matches!(
        error,
        TypeError::UnsupportedFeature { feature, .. } if *feature == "generic structs"
    )));
    assert!(errors.iter().any(|error| matches!(
        error,
        TypeError::UnsupportedFeature { feature, .. } if *feature == "generic impl blocks"
    )));
}

#[test]
fn reports_top_level_let_as_unsupported() {
    let syms = symbols();
    let mut ast = AstFactory::new();

    let top_level_let = Decl::Let(LetDecl {
        id: ast.id(),
        span: span(),
        name: syms.value,
        ty: Some(ast.named_type(syms.i32_)),
        initializer: ast.int(1),
    });

    let mut checker = checker_with_symbols();
    let errors = checker
        .check(&[top_level_let])
        .expect_err("top-level let should not type check");
    assert!(errors.iter().any(|error| matches!(
        error,
        TypeError::UnsupportedFeature { feature, .. } if *feature == "top-level let declarations"
    )));
}

#[test]
fn reports_break_and_continue_outside_loops() {
    let syms = symbols();
    let mut ast = AstFactory::new();
    let break_stmt = Stmt::Break(BreakStmt {
        id: ast.id(),
        span: span(),
    });
    let continue_stmt = Stmt::Continue(ContinueStmt {
        id: ast.id(),
        span: span(),
    });
    let zero = ast.int(0);
    let ret_stmt = return_stmt(&mut ast, Some(zero));

    let body = ast.block(vec![break_stmt, continue_stmt, ret_stmt]);
    let func = Decl::Func(FuncDecl {
        id: ast.id(),
        span: span(),
        name: syms.foo,
        params: leak_slice(vec![]),
        return_type: Some(ast.named_type(syms.i32_)),
        body,
    });

    let mut checker = checker_with_symbols();
    let errors = checker
        .check(&[func])
        .expect_err("break/continue outside loops should fail");
    assert!(errors.iter().any(|error| matches!(
        error,
        TypeError::InvalidControlFlow { message, .. } if *message == "break can only be used inside a loop"
    )));
    assert!(errors.iter().any(|error| matches!(
        error,
        TypeError::InvalidControlFlow { message, .. } if *message == "continue can only be used inside a loop"
    )));
}

#[test]
fn reports_unsupported_assignment_targets() {
    let syms = symbols();
    let mut ast = AstFactory::new();

    let point_ty = ast.named_type(syms.point);
    let point_struct = Decl::Struct(StructDecl {
        id: ast.id(),
        span: span(),
        name: syms.point,
        fields: leak_slice(vec![FieldDef {
            id: ast.id(),
            span: span(),
            name: syms.x,
            ty: ast.named_type(syms.i32_),
        }]),
    });

    let nested_target = {
        let point = ast.ident(syms.point);
        let inner = ast.field(point, syms.x);
        ast.field(inner, syms.x)
    };
    let nested_value = ast.int(1);
    let nested_assign_expr = ast.assign(nested_target, nested_value);
    let assign_nested = expr_stmt(&mut ast, nested_assign_expr);
    let index_target = {
        let point = ast.ident(syms.point);
        let zero = ast.int(0);
        ast.index(point, zero)
    };
    let index_value = ast.int(2);
    let index_assign_expr = ast.assign(index_target, index_value);
    let assign_index = expr_stmt(&mut ast, index_assign_expr);
    let body = ast.block(vec![assign_nested, assign_index]);
    let func = Decl::Func(FuncDecl {
        id: ast.id(),
        span: span(),
        name: syms.foo,
        params: leak_slice(vec![Param {
            id: ast.id(),
            span: span(),
            name: syms.point,
            ty: point_ty,
        }]),
        return_type: Some(ast.named_type(syms.i32_)),
        body,
    });

    let mut checker = checker_with_symbols();
    let errors = checker
        .check(&[point_struct, func])
        .expect_err("nested/index assignment targets should fail");
    assert!(errors.iter().filter(|error| matches!(error, TypeError::InvalidAssignmentTarget { .. })).count() >= 2);
}

#[test]
fn reports_non_constant_top_level_const_initializers() {
    let syms = symbols();
    let mut ast = AstFactory::new();

    let const_from_literal = Decl::Const(ConstDecl {
        id: ast.id(),
        span: span(),
        name: syms.x,
        type_ann: ast.named_type(syms.i32_),
        value: ast.int(1),
    });
    let non_const_value = {
        let callee = ast.ident(syms.foo);
        ast.call(callee, vec![])
    };
    let const_from_call = Decl::Const(ConstDecl {
        id: ast.id(),
        span: span(),
        name: syms.value,
        type_ann: ast.named_type(syms.i32_),
        value: non_const_value,
    });
    let const_from_const = Decl::Const(ConstDecl {
        id: ast.id(),
        span: span(),
        name: syms.y,
        type_ann: ast.named_type(syms.i32_),
        value: ast.ident(syms.x),
    });

    let mut checker = checker_with_symbols();
    let errors = checker
        .check(&[const_from_literal, const_from_call, const_from_const])
        .expect_err("non-constant const initializer should fail");
    assert!(errors.iter().any(|error| matches!(
        error,
        TypeError::InvalidConstInitializer { .. }
    )));
}

#[test]
fn reports_value_returns_inside_intent_and_flow() {
    let syms = symbols();
    let mut ast = AstFactory::new();
    let intent_value = ast.int(1);
    let intent_return = return_stmt(&mut ast, Some(intent_value));
    let intent_body = ast.block(vec![intent_return]);

    let intent = Decl::Intent(thagore_ast::IntentDecl {
        id: ast.id(),
        span: span(),
        name: syms.optimize,
        constraints: leak_slice(vec![]),
        body: intent_body,
    });

    let flow_value = ast.int(2);
    let flow_return = return_stmt(&mut ast, Some(flow_value));
    let flow_body = ast.block(vec![flow_return]);
    let flow = Decl::Flow(FlowDecl {
        id: ast.id(),
        span: span(),
        name: syms.bar,
        stages: leak_slice(vec![FlowStage {
            id: ast.id(),
            span: span(),
            name: syms.stage,
            body: flow_body,
        }]),
        compensation: None,
    });

    let mut checker = checker_with_symbols();
    let errors = checker
        .check(&[intent, flow])
        .expect_err("value returns inside intent/flow should fail");

    assert!(errors.iter().filter(|error| matches!(
        error,
        TypeError::ReturnTypeMismatch { .. }
    )).count() >= 2);
}
