use bumpalo::{collections::Vec as BumpVec, Bump};
use thagore_ast::{
    walk_decl, AssignExpr, BinOp, BinaryExpr, Block, CallExpr, Decl, Expr, ExprStmt, ExternDecl,
    FieldAccessExpr, FieldDef, FlowDecl, FlowStage, ForStmt, FuncDecl, GenericTypeExpr, IdentExpr,
    IfStmt, ImplBlock, ImportDecl, ImportSymbol, InferTypeExpr, InternedStr, LetDecl, LitExpr, Literal,
    NamedTypeExpr, NodeId, Param, ReturnStmt, Span, Stmt, StructDecl, TypeExpr, UnaryExpr, UnaryOp,
    Visitor, WhileStmt,
};

fn span(start: u32, end: u32) -> Span {
    Span::new(start, end)
}

fn symbol(index: u32) -> InternedStr {
    InternedStr::new(index)
}

fn arena_slice<'ast, T>(arena: &'ast Bump, items: impl IntoIterator<Item = T>) -> &'ast [T] {
    let mut values = BumpVec::new_in(arena);
    values.extend(items);
    values.into_bump_slice()
}

fn alloc_expr<'ast>(arena: &'ast Bump, expr: Expr<'ast>) -> &'ast Expr<'ast> {
    arena.alloc(expr)
}

fn alloc_type<'ast>(arena: &'ast Bump, ty: TypeExpr<'ast>) -> &'ast TypeExpr<'ast> {
    arena.alloc(ty)
}

fn alloc_block<'ast>(arena: &'ast Bump, block: Block<'ast>) -> &'ast Block<'ast> {
    arena.alloc(block)
}

#[test]
fn constructs_every_ast_node_type() {
    let arena = Bump::new();

    let ty_i32 = alloc_type(
        &arena,
        TypeExpr::Named(NamedTypeExpr {
            id: NodeId::new(1),
            span: span(0, 3),
            name: symbol(1),
        }),
    );
    let ty_f64 = alloc_type(
        &arena,
        TypeExpr::Named(NamedTypeExpr {
            id: NodeId::new(2),
            span: span(4, 7),
            name: symbol(2),
        }),
    );
    let ty_infer = alloc_type(
        &arena,
        TypeExpr::Infer(InferTypeExpr {
            id: NodeId::new(3),
            span: span(8, 9),
        }),
    );
    let ty_generic_arg = alloc_type(
        &arena,
        TypeExpr::Named(NamedTypeExpr {
            id: NodeId::new(4),
            span: span(10, 13),
            name: symbol(3),
        }),
    );
    let ty_generic_args = arena_slice(&arena, [ty_generic_arg]);
    let ty_generic = alloc_type(
        &arena,
        TypeExpr::Generic(GenericTypeExpr {
            id: NodeId::new(5),
            span: span(14, 24),
            name: symbol(4),
            args: ty_generic_args,
        }),
    );

    let ident_expr = alloc_expr(
        &arena,
        Expr::Ident(IdentExpr {
            id: NodeId::new(10),
            span: span(25, 29),
            name: symbol(10),
        }),
    );
    let int_lit = alloc_expr(
        &arena,
        Expr::Literal(LitExpr {
            id: NodeId::new(11),
            span: span(30, 31),
            literal: Literal::Int(1),
        }),
    );
    let float_lit = alloc_expr(
        &arena,
        Expr::Literal(LitExpr {
            id: NodeId::new(12),
            span: span(32, 35),
            literal: Literal::Float(2.5),
        }),
    );
    let bool_lit = alloc_expr(
        &arena,
        Expr::Literal(LitExpr {
            id: NodeId::new(13),
            span: span(36, 40),
            literal: Literal::Bool(true),
        }),
    );
    let str_lit = alloc_expr(
        &arena,
        Expr::Literal(LitExpr {
            id: NodeId::new(14),
            span: span(41, 47),
            literal: Literal::Str(symbol(11)),
        }),
    );
    let unary_expr = alloc_expr(
        &arena,
        Expr::Unary(UnaryExpr {
            id: NodeId::new(15),
            span: span(48, 50),
            op: UnaryOp::Neg,
            operand: int_lit,
        }),
    );
    let binary_expr = alloc_expr(
        &arena,
        Expr::Binary(BinaryExpr {
            id: NodeId::new(16),
            span: span(51, 56),
            left: ident_expr,
            op: BinOp::Add,
            right: unary_expr,
        }),
    );
    let call_args = arena_slice(&arena, [binary_expr, float_lit, bool_lit]);
    let call_expr = alloc_expr(
        &arena,
        Expr::Call(CallExpr {
            id: NodeId::new(17),
            span: span(57, 70),
            callee: ident_expr,
            args: call_args,
        }),
    );
    let field_access = alloc_expr(
        &arena,
        Expr::FieldAccess(FieldAccessExpr {
            id: NodeId::new(18),
            span: span(71, 82),
            object: call_expr,
            field: symbol(12),
        }),
    );
    let index_expr = alloc_expr(
        &arena,
        Expr::Index(thagore_ast::IndexExpr {
            id: NodeId::new(19),
            span: span(83, 90),
            object: field_access,
            index: int_lit,
        }),
    );
    let assign_expr = alloc_expr(
        &arena,
        Expr::Assign(AssignExpr {
            id: NodeId::new(20),
            span: span(91, 101),
            target: ident_expr,
            value: index_expr,
        }),
    );

    let expr_stmt = Stmt::Expr(ExprStmt {
        id: NodeId::new(30),
        span: span(102, 110),
        expr: assign_expr,
    });
    let return_stmt = Stmt::Return(ReturnStmt {
        id: NodeId::new(31),
        span: span(111, 120),
        value: Some(str_lit),
    });
    let while_body = alloc_block(
        &arena,
        Block {
            id: NodeId::new(32),
            span: span(121, 130),
            statements: arena_slice(&arena, [expr_stmt.clone()]),
        },
    );
    let while_stmt = Stmt::While(WhileStmt {
        id: NodeId::new(33),
        span: span(131, 145),
        condition: bool_lit,
        body: while_body,
    });
    let for_body = alloc_block(
        &arena,
        Block {
            id: NodeId::new(34),
            span: span(146, 156),
            statements: arena_slice(&arena, [return_stmt.clone()]),
        },
    );
    let for_stmt = Stmt::For(ForStmt {
        id: NodeId::new(35),
        span: span(157, 172),
        binding: symbol(13),
        iterator: call_expr,
        body: for_body,
    });
    let then_block = alloc_block(
        &arena,
        Block {
            id: NodeId::new(36),
            span: span(173, 190),
            statements: arena_slice(&arena, [while_stmt.clone()]),
        },
    );
    let else_block = alloc_block(
        &arena,
        Block {
            id: NodeId::new(37),
            span: span(191, 208),
            statements: arena_slice(&arena, [for_stmt.clone()]),
        },
    );
    let if_stmt = Stmt::If(IfStmt {
        id: NodeId::new(38),
        span: span(209, 230),
        condition: binary_expr,
        then_block,
        else_block: Some(else_block),
    });
    let stmt_let = Stmt::Let(LetDecl {
        id: NodeId::new(39),
        span: span(231, 245),
        name: symbol(14),
        ty: Some(ty_infer),
        initializer: assign_expr,
    });
    let body_block = alloc_block(
        &arena,
        Block {
            id: NodeId::new(40),
            span: span(246, 300),
            statements: arena_slice(
                &arena,
                [
                    stmt_let.clone(),
                    expr_stmt.clone(),
                    if_stmt.clone(),
                    return_stmt.clone(),
                ],
            ),
        },
    );

    let params = arena_slice(
        &arena,
        [
            Param {
                id: NodeId::new(50),
                span: span(301, 310),
                name: symbol(15),
                ty: ty_i32,
            },
            Param {
                id: NodeId::new(51),
                span: span(311, 320),
                name: symbol(16),
                ty: ty_generic,
            },
        ],
    );
    let fields = arena_slice(
        &arena,
        [
            FieldDef {
                id: NodeId::new(52),
                span: span(321, 330),
                name: symbol(17),
                ty: ty_i32,
            },
            FieldDef {
                id: NodeId::new(53),
                span: span(331, 340),
                name: symbol(18),
                ty: ty_f64,
            },
        ],
    );

    let func_decl = Decl::Func(FuncDecl {
        id: NodeId::new(60),
        span: span(341, 400),
        name: symbol(19),
        params,
        return_type: Some(ty_i32),
        body: body_block,
    });
    let top_let = Decl::Let(LetDecl {
        id: NodeId::new(61),
        span: span(401, 415),
        name: symbol(20),
        ty: Some(ty_i32),
        initializer: int_lit,
    });
    let struct_decl = Decl::Struct(StructDecl {
        id: NodeId::new(62),
        span: span(416, 450),
        name: symbol(21),
        fields,
    });
    let impl_decl = Decl::Impl(ImplBlock {
        id: NodeId::new(63),
        span: span(451, 510),
        target: symbol(21),
        methods: arena_slice(
            &arena,
            [FuncDecl {
                id: NodeId::new(64),
                span: span(511, 560),
                name: symbol(22),
                params,
                return_type: Some(ty_i32),
                body: body_block,
            }],
        ),
    });
    let import_decl = Decl::Import(ImportDecl {
        id: NodeId::new(65),
        span: span(561, 575),
        relative_level: 0,
        path_segments: arena_slice(&arena, [symbol(23), symbol(24)]),
        symbols: arena_slice(
            &arena,
            [ImportSymbol {
                id: NodeId::new(69),
                span: span(576, 580),
                name: symbol(30),
                alias: Some(symbol(31)),
            }],
        ),
        is_from: true,
        include_all: false,
        alias: Some(symbol(29)),
    });
    let extern_decl = Decl::Extern(ExternDecl {
        id: NodeId::new(66),
        span: span(576, 600),
        name: symbol(25),
        params,
        return_type: ty_i32,
    });
    let intent_decl = Decl::Intent(thagore_ast::IntentDecl {
        id: NodeId::new(67),
        span: span(601, 640),
        name: symbol(26),
        constraints: arena_slice(&arena, [binary_expr, bool_lit]),
        body: body_block,
    });
    let flow_decl = Decl::Flow(FlowDecl {
        id: NodeId::new(68),
        span: span(641, 700),
        name: symbol(27),
        stages: arena_slice(
            &arena,
            [FlowStage {
                id: NodeId::new(69),
                span: span(701, 730),
                name: symbol(28),
                body: body_block,
            }],
        ),
        compensation: Some(body_block),
    });

    assert!(matches!(func_decl, Decl::Func(_)));
    assert!(matches!(top_let, Decl::Let(_)));
    assert!(matches!(struct_decl, Decl::Struct(_)));
    assert!(matches!(impl_decl, Decl::Impl(_)));
    assert!(matches!(import_decl, Decl::Import(_)));
    assert!(matches!(extern_decl, Decl::Extern(_)));
    assert!(matches!(intent_decl, Decl::Intent(_)));
    assert!(matches!(flow_decl, Decl::Flow(_)));
    assert_eq!(binary_expr.id(), NodeId::new(16));
    assert_eq!(body_block.id, NodeId::new(40));
}

#[derive(Default)]
struct CountingVisitor {
    decls: usize,
    funcs: usize,
    lets: usize,
    structs: usize,
    params: usize,
    fields: usize,
    blocks: usize,
    stmts: usize,
    ifs: usize,
    exprs: usize,
    binaries: usize,
    returns: usize,
    types: usize,
    generics: usize,
}

impl<'ast> Visitor<'ast> for CountingVisitor {
    fn visit_decl(&mut self, _decl: &'ast Decl<'ast>) {
        self.decls += 1;
    }

    fn visit_func_decl(&mut self, _decl: &'ast FuncDecl<'ast>) {
        self.funcs += 1;
    }

    fn visit_let_decl(&mut self, _decl: &'ast LetDecl<'ast>) {
        self.lets += 1;
    }

    fn visit_struct_decl(&mut self, _decl: &'ast StructDecl<'ast>) {
        self.structs += 1;
    }

    fn visit_param(&mut self, _param: &'ast Param<'ast>) {
        self.params += 1;
    }

    fn visit_field_def(&mut self, _field: &'ast FieldDef<'ast>) {
        self.fields += 1;
    }

    fn visit_block(&mut self, _block: &'ast Block<'ast>) {
        self.blocks += 1;
    }

    fn visit_stmt(&mut self, _stmt: &'ast Stmt<'ast>) {
        self.stmts += 1;
    }

    fn visit_if_stmt(&mut self, _stmt: &'ast IfStmt<'ast>) {
        self.ifs += 1;
    }

    fn visit_return_stmt(&mut self, _stmt: &'ast ReturnStmt<'ast>) {
        self.returns += 1;
    }

    fn visit_expr(&mut self, _expr: &'ast Expr<'ast>) {
        self.exprs += 1;
    }

    fn visit_binary_expr(&mut self, _expr: &'ast BinaryExpr<'ast>) {
        self.binaries += 1;
    }

    fn visit_type_expr(&mut self, _ty: &'ast TypeExpr<'ast>) {
        self.types += 1;
    }

    fn visit_generic_type_expr(&mut self, _ty: &'ast GenericTypeExpr<'ast>) {
        self.generics += 1;
    }
}

#[test]
fn visitor_walk_traverses_nested_nodes() {
    let arena = Bump::new();

    let ty_i32 = alloc_type(
        &arena,
        TypeExpr::Named(NamedTypeExpr {
            id: NodeId::new(1),
            span: span(0, 3),
            name: symbol(1),
        }),
    );
    let ty_bool = alloc_type(
        &arena,
        TypeExpr::Named(NamedTypeExpr {
            id: NodeId::new(2),
            span: span(4, 8),
            name: symbol(2),
        }),
    );
    let ty_list = alloc_type(
        &arena,
        TypeExpr::Generic(GenericTypeExpr {
            id: NodeId::new(3),
            span: span(9, 20),
            name: symbol(3),
            args: arena_slice(&arena, [ty_i32]),
        }),
    );
    let lhs = alloc_expr(
        &arena,
        Expr::Ident(IdentExpr {
            id: NodeId::new(4),
            span: span(21, 22),
            name: symbol(4),
        }),
    );
    let rhs = alloc_expr(
        &arena,
        Expr::Literal(LitExpr {
            id: NodeId::new(5),
            span: span(23, 24),
            literal: Literal::Int(1),
        }),
    );
    let cond = alloc_expr(
        &arena,
        Expr::Binary(BinaryExpr {
            id: NodeId::new(6),
            span: span(21, 24),
            left: lhs,
            op: BinOp::Gt,
            right: rhs,
        }),
    );
    let ret_stmt = Stmt::Return(ReturnStmt {
        id: NodeId::new(7),
        span: span(25, 33),
        value: Some(lhs),
    });
    let then_block = alloc_block(
        &arena,
        Block {
            id: NodeId::new(8),
            span: span(34, 45),
            statements: arena_slice(&arena, [ret_stmt]),
        },
    );
    let if_stmt = Stmt::If(IfStmt {
        id: NodeId::new(9),
        span: span(46, 70),
        condition: cond,
        then_block,
        else_block: None,
    });
    let local_let = Stmt::Let(LetDecl {
        id: NodeId::new(10),
        span: span(71, 90),
        name: symbol(5),
        ty: Some(ty_bool),
        initializer: cond,
    });
    let body = alloc_block(
        &arena,
        Block {
            id: NodeId::new(11),
            span: span(91, 130),
            statements: arena_slice(&arena, [local_let, if_stmt]),
        },
    );

    let decl = Decl::Func(FuncDecl {
        id: NodeId::new(12),
        span: span(131, 200),
        name: symbol(6),
        params: arena_slice(
            &arena,
            [Param {
                id: NodeId::new(13),
                span: span(201, 210),
                name: symbol(7),
                ty: ty_list,
            }],
        ),
        return_type: Some(ty_i32),
        body,
    });
    let decl_ref = arena.alloc(decl);

    let struct_decl = Decl::Struct(StructDecl {
        id: NodeId::new(14),
        span: span(211, 240),
        name: symbol(8),
        fields: arena_slice(
            &arena,
            [FieldDef {
                id: NodeId::new(15),
                span: span(241, 250),
                name: symbol(9),
                ty: ty_i32,
            }],
        ),
    });
    let struct_ref = arena.alloc(struct_decl);

    let mut visitor = CountingVisitor::default();
    walk_decl(&mut visitor, decl_ref);
    walk_decl(&mut visitor, struct_ref);

    assert_eq!(visitor.decls, 2);
    assert_eq!(visitor.funcs, 1);
    assert_eq!(visitor.lets, 1);
    assert_eq!(visitor.structs, 1);
    assert_eq!(visitor.params, 1);
    assert_eq!(visitor.fields, 1);
    assert_eq!(visitor.blocks, 2);
    assert_eq!(visitor.stmts, 3);
    assert_eq!(visitor.ifs, 1);
    assert_eq!(visitor.returns, 1);
    assert_eq!(visitor.binaries, 2);
    assert_eq!(visitor.generics, 1);
    assert!(visitor.exprs >= 5);
    assert!(visitor.types >= 5);
}

#[test]
fn display_pretty_prints_non_trivial_program() {
    let arena = Bump::new();

    let ty_i32 = alloc_type(
        &arena,
        TypeExpr::Named(NamedTypeExpr {
            id: NodeId::new(1),
            span: span(0, 3),
            name: symbol(100),
        }),
    );
    let import = Decl::Import(ImportDecl {
        id: NodeId::new(2),
        span: span(4, 20),
        relative_level: 0,
        path_segments: arena_slice(&arena, [symbol(1), symbol(2)]),
        symbols: arena_slice(&arena, []),
        is_from: false,
        include_all: false,
        alias: Some(symbol(9)),
    });
    let structure = Decl::Struct(StructDecl {
        id: NodeId::new(3),
        span: span(21, 50),
        name: symbol(3),
        fields: arena_slice(
            &arena,
            [
                FieldDef {
                    id: NodeId::new(4),
                    span: span(51, 60),
                    name: symbol(4),
                    ty: ty_i32,
                },
                FieldDef {
                    id: NodeId::new(5),
                    span: span(61, 70),
                    name: symbol(5),
                    ty: ty_i32,
                },
            ],
        ),
    });
    let ident_value = alloc_expr(
        &arena,
        Expr::Ident(IdentExpr {
            id: NodeId::new(6),
            span: span(71, 76),
            name: symbol(6),
        }),
    );
    let literal_one = alloc_expr(
        &arena,
        Expr::Literal(LitExpr {
            id: NodeId::new(7),
            span: span(77, 78),
            literal: Literal::Int(1),
        }),
    );
    let add_expr = alloc_expr(
        &arena,
        Expr::Binary(BinaryExpr {
            id: NodeId::new(8),
            span: span(79, 86),
            left: ident_value,
            op: BinOp::Add,
            right: literal_one,
        }),
    );
    let zero_expr = alloc_expr(
        &arena,
        Expr::Literal(LitExpr {
            id: NodeId::new(9),
            span: span(87, 88),
            literal: Literal::Int(0),
        }),
    );
    let compare_expr = alloc_expr(
        &arena,
        Expr::Binary(BinaryExpr {
            id: NodeId::new(10),
            span: span(89, 95),
            left: add_expr,
            op: BinOp::Gt,
            right: zero_expr,
        }),
    );
    let let_stmt = Stmt::Let(LetDecl {
        id: NodeId::new(11),
        span: span(96, 110),
        name: symbol(7),
        ty: Some(ty_i32),
        initializer: add_expr,
    });
    let return_then = Stmt::Return(ReturnStmt {
        id: NodeId::new(12),
        span: span(111, 120),
        value: Some(add_expr),
    });
    let return_else = Stmt::Return(ReturnStmt {
        id: NodeId::new(13),
        span: span(121, 130),
        value: Some(zero_expr),
    });
    let then_block = alloc_block(
        &arena,
        Block {
            id: NodeId::new(14),
            span: span(131, 145),
            statements: arena_slice(&arena, [return_then]),
        },
    );
    let else_block = alloc_block(
        &arena,
        Block {
            id: NodeId::new(15),
            span: span(146, 160),
            statements: arena_slice(&arena, [return_else]),
        },
    );
    let if_stmt = Stmt::If(IfStmt {
        id: NodeId::new(16),
        span: span(161, 190),
        condition: compare_expr,
        then_block,
        else_block: Some(else_block),
    });
    let body = alloc_block(
        &arena,
        Block {
            id: NodeId::new(17),
            span: span(191, 250),
            statements: arena_slice(&arena, [let_stmt, if_stmt]),
        },
    );
    let function = Decl::Func(FuncDecl {
        id: NodeId::new(18),
        span: span(251, 330),
        name: symbol(8),
        params: arena_slice(
            &arena,
            [Param {
                id: NodeId::new(19),
                span: span(331, 340),
                name: symbol(6),
                ty: ty_i32,
            }],
        ),
        return_type: Some(ty_i32),
        body,
    });

    let rendered = format!("{import}\n\n{structure}\n\n{function}");
    let expected = "\
import sym_1.sym_2 as sym_9

struct sym_3:
    sym_4: sym_100
    sym_5: sym_100

func sym_8(sym_6: sym_100) -> sym_100:
    let sym_7: sym_100 = sym_6 + 1
    if (sym_6 + 1 > 0):
        return sym_6 + 1
    else:
        return 0";

    assert_eq!(rendered, expected);
}
