use bumpalo::{collections::Vec as BumpVec, Bump};
use thagore_ast::{
    walk_decl, AssignExpr, BinOp, BinaryExpr, Block, CallExpr, ConstDecl, Constraint,
    ConstraintKind, Decl, Expr, ExprStmt, ExternDecl, FieldAccessExpr, FieldDef, FlowDecl,
    FlowStage, ForStmt, FuncDecl, GenericFuncDecl, GenericImplBlock, GenericStructDecl,
    GenericTypeExpr, IdentExpr, IfStmt, ImplBlock, ImportDecl, ImportSymbol, InferTypeExpr,
    InternedStr, LetDecl, LitExpr, Literal, NamedTypeExpr, NodeId, Param, ReturnStmt, Span, Stmt,
    StructDecl, TypeExpr, TypeParam, UnaryExpr, UnaryOp, Visitor, WhileStmt,
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

fn ordered_constraint(id: u32, start: u32, end: u32) -> Constraint {
    Constraint {
        id: NodeId::new(id),
        span: span(start, end),
        kind: ConstraintKind::Ordered,
    }
}

fn numeric_constraint(id: u32, start: u32, end: u32) -> Constraint {
    Constraint {
        id: NodeId::new(id),
        span: span(start, end),
        kind: ConstraintKind::Numeric,
    }
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
    let ty_generic = alloc_type(
        &arena,
        TypeExpr::Generic(GenericTypeExpr {
            id: NodeId::new(4),
            span: span(10, 20),
            name: symbol(3),
            args: arena_slice(&arena, [ty_i32, ty_f64]),
        }),
    );

    let type_params = arena_slice(
        &arena,
        [
            TypeParam {
                id: NodeId::new(5),
                span: span(21, 31),
                name: symbol(4),
                constraints: arena_slice(&arena, [numeric_constraint(6, 24, 31)]),
            },
            TypeParam {
                id: NodeId::new(7),
                span: span(32, 44),
                name: symbol(5),
                constraints: arena_slice(
                    &arena,
                    [ordered_constraint(8, 35, 42), Constraint {
                        id: NodeId::new(9),
                        span: span(45, 47),
                        kind: ConstraintKind::Eq,
                    }],
                ),
            },
        ],
    );

    let ident_expr = alloc_expr(
        &arena,
        Expr::Ident(IdentExpr {
            id: NodeId::new(10),
            span: span(48, 49),
            name: symbol(10),
        }),
    );
    let int_lit = alloc_expr(
        &arena,
        Expr::Literal(LitExpr {
            id: NodeId::new(11),
            span: span(50, 51),
            literal: Literal::Int(1),
        }),
    );
    let float_lit = alloc_expr(
        &arena,
        Expr::Literal(LitExpr {
            id: NodeId::new(12),
            span: span(52, 56),
            literal: Literal::Float(2.5),
        }),
    );
    let bool_lit = alloc_expr(
        &arena,
        Expr::Literal(LitExpr {
            id: NodeId::new(13),
            span: span(57, 61),
            literal: Literal::Bool(true),
        }),
    );
    let str_lit = alloc_expr(
        &arena,
        Expr::Literal(LitExpr {
            id: NodeId::new(14),
            span: span(62, 69),
            literal: Literal::Str(symbol(11)),
        }),
    );
    let unary_expr = alloc_expr(
        &arena,
        Expr::Unary(UnaryExpr {
            id: NodeId::new(15),
            span: span(70, 72),
            op: UnaryOp::Neg,
            operand: int_lit,
        }),
    );
    let binary_expr = alloc_expr(
        &arena,
        Expr::Binary(BinaryExpr {
            id: NodeId::new(16),
            span: span(73, 79),
            left: ident_expr,
            op: BinOp::Add,
            right: unary_expr,
        }),
    );
    let call_expr = alloc_expr(
        &arena,
        Expr::Call(CallExpr {
            id: NodeId::new(17),
            span: span(80, 95),
            callee: ident_expr,
            args: arena_slice(&arena, [binary_expr, float_lit, bool_lit]),
        }),
    );
    let field_access = alloc_expr(
        &arena,
        Expr::FieldAccess(FieldAccessExpr {
            id: NodeId::new(18),
            span: span(96, 104),
            object: call_expr,
            field: symbol(12),
        }),
    );
    let index_expr = alloc_expr(
        &arena,
        Expr::Index(thagore_ast::IndexExpr {
            id: NodeId::new(19),
            span: span(105, 110),
            object: field_access,
            index: int_lit,
        }),
    );
    let assign_expr = alloc_expr(
        &arena,
        Expr::Assign(AssignExpr {
            id: NodeId::new(20),
            span: span(111, 120),
            target: ident_expr,
            value: index_expr,
        }),
    );

    let expr_stmt = Stmt::Expr(ExprStmt {
        id: NodeId::new(21),
        span: span(121, 129),
        expr: assign_expr,
    });
    let return_stmt = Stmt::Return(ReturnStmt {
        id: NodeId::new(22),
        span: span(130, 140),
        value: Some(str_lit),
    });
    let while_body = alloc_block(
        &arena,
        Block {
            id: NodeId::new(23),
            span: span(141, 150),
            statements: arena_slice(&arena, [expr_stmt.clone()]),
        },
    );
    let while_stmt = Stmt::While(WhileStmt {
        id: NodeId::new(24),
        span: span(151, 164),
        condition: bool_lit,
        body: while_body,
    });
    let for_body = alloc_block(
        &arena,
        Block {
            id: NodeId::new(25),
            span: span(165, 175),
            statements: arena_slice(&arena, [return_stmt.clone()]),
        },
    );
    let for_stmt = Stmt::For(ForStmt {
        id: NodeId::new(26),
        span: span(176, 191),
        binding: symbol(13),
        iterator: call_expr,
        body: for_body,
    });
    let then_block = alloc_block(
        &arena,
        Block {
            id: NodeId::new(27),
            span: span(192, 207),
            statements: arena_slice(&arena, [while_stmt.clone()]),
        },
    );
    let else_block = alloc_block(
        &arena,
        Block {
            id: NodeId::new(28),
            span: span(208, 223),
            statements: arena_slice(&arena, [for_stmt.clone()]),
        },
    );
    let if_stmt = Stmt::If(IfStmt {
        id: NodeId::new(29),
        span: span(224, 245),
        condition: binary_expr,
        then_block,
        else_block: Some(else_block),
    });
    let stmt_let = Stmt::Let(LetDecl {
        id: NodeId::new(30),
        span: span(246, 261),
        name: symbol(14),
        ty: Some(ty_infer),
        initializer: assign_expr,
    });
    let body_block = alloc_block(
        &arena,
        Block {
            id: NodeId::new(31),
            span: span(262, 320),
            statements: arena_slice(
                &arena,
                [stmt_let.clone(), expr_stmt.clone(), if_stmt.clone(), return_stmt.clone()],
            ),
        },
    );

    let params = arena_slice(
        &arena,
        [
            Param {
                id: NodeId::new(32),
                span: span(321, 330),
                name: symbol(15),
                ty: ty_i32,
            },
            Param {
                id: NodeId::new(33),
                span: span(331, 343),
                name: symbol(16),
                ty: ty_generic,
            },
        ],
    );
    let fields = arena_slice(
        &arena,
        [
            FieldDef {
                id: NodeId::new(34),
                span: span(344, 353),
                name: symbol(17),
                ty: ty_i32,
            },
            FieldDef {
                id: NodeId::new(35),
                span: span(354, 363),
                name: symbol(18),
                ty: ty_f64,
            },
        ],
    );

    let func_decl = Decl::Func(FuncDecl {
        id: NodeId::new(36),
        span: span(364, 430),
        name: symbol(19),
        params,
        return_type: Some(ty_i32),
        body: body_block,
    });
    let generic_func_decl = Decl::GenericFunc(GenericFuncDecl {
        id: NodeId::new(37),
        span: span(431, 505),
        name: symbol(20),
        type_params,
        params,
        return_type: Some(ty_generic),
        body: body_block,
    });
    let top_let = Decl::Let(LetDecl {
        id: NodeId::new(38),
        span: span(506, 520),
        name: symbol(21),
        ty: Some(ty_i32),
        initializer: int_lit,
    });
    let const_decl = Decl::Const(ConstDecl {
        id: NodeId::new(39),
        span: span(521, 540),
        name: symbol(22),
        type_ann: ty_f64,
        value: float_lit,
    });
    let struct_decl = Decl::Struct(StructDecl {
        id: NodeId::new(40),
        span: span(541, 575),
        name: symbol(23),
        fields,
    });
    let generic_struct_decl = Decl::GenericStruct(GenericStructDecl {
        id: NodeId::new(41),
        span: span(576, 620),
        name: symbol(24),
        type_params,
        fields,
    });
    let impl_decl = Decl::Impl(ImplBlock {
        id: NodeId::new(42),
        span: span(621, 690),
        target: symbol(23),
        methods: arena_slice(
            &arena,
            [FuncDecl {
                id: NodeId::new(43),
                span: span(691, 735),
                name: symbol(25),
                params,
                return_type: Some(ty_i32),
                body: body_block,
            }],
        ),
    });
    let generic_impl_decl = Decl::GenericImpl(GenericImplBlock {
        id: NodeId::new(44),
        span: span(736, 810),
        target: symbol(24),
        type_params,
        methods: arena_slice(
            &arena,
            [FuncDecl {
                id: NodeId::new(45),
                span: span(811, 860),
                name: symbol(26),
                params,
                return_type: Some(ty_generic),
                body: body_block,
            }],
        ),
    });
    let import_decl = Decl::Import(ImportDecl {
        id: NodeId::new(46),
        span: span(861, 878),
        relative_level: 0,
        path_segments: arena_slice(&arena, [symbol(27), symbol(28)]),
        symbols: arena_slice(
            &arena,
            [ImportSymbol {
                id: NodeId::new(47),
                span: span(879, 884),
                name: symbol(29),
                alias: Some(symbol(30)),
            }],
        ),
        is_from: true,
        include_all: false,
        alias: None,
    });
    let extern_decl = Decl::Extern(ExternDecl {
        id: NodeId::new(48),
        span: span(885, 915),
        name: symbol(31),
        params,
        return_type: ty_i32,
    });
    let intent_decl = Decl::Intent(thagore_ast::IntentDecl {
        id: NodeId::new(49),
        span: span(916, 960),
        name: symbol(32),
        constraints: arena_slice(&arena, [binary_expr, bool_lit]),
        body: body_block,
    });
    let flow_decl = Decl::Flow(FlowDecl {
        id: NodeId::new(50),
        span: span(961, 1015),
        name: symbol(33),
        stages: arena_slice(
            &arena,
            [FlowStage {
                id: NodeId::new(51),
                span: span(1016, 1045),
                name: symbol(34),
                body: body_block,
            }],
        ),
        compensation: Some(body_block),
    });

    assert!(matches!(func_decl, Decl::Func(_)));
    assert!(matches!(generic_func_decl, Decl::GenericFunc(_)));
    assert!(matches!(top_let, Decl::Let(_)));
    assert!(matches!(const_decl, Decl::Const(_)));
    assert!(matches!(struct_decl, Decl::Struct(_)));
    assert!(matches!(generic_struct_decl, Decl::GenericStruct(_)));
    assert!(matches!(impl_decl, Decl::Impl(_)));
    assert!(matches!(generic_impl_decl, Decl::GenericImpl(_)));
    assert!(matches!(import_decl, Decl::Import(_)));
    assert!(matches!(extern_decl, Decl::Extern(_)));
    assert!(matches!(intent_decl, Decl::Intent(_)));
    assert!(matches!(flow_decl, Decl::Flow(_)));
    assert_eq!(binary_expr.id(), NodeId::new(16));
    assert_eq!(body_block.id, NodeId::new(31));
}

#[derive(Default)]
struct CountingVisitor {
    decls: usize,
    generic_funcs: usize,
    consts: usize,
    generic_structs: usize,
    generic_impls: usize,
    type_params: usize,
    constraints: usize,
    blocks: usize,
    exprs: usize,
    types: usize,
}

impl<'ast> Visitor<'ast> for CountingVisitor {
    fn visit_decl(&mut self, _decl: &'ast Decl<'ast>) {
        self.decls += 1;
    }

    fn visit_generic_func_decl(&mut self, _decl: &'ast GenericFuncDecl<'ast>) {
        self.generic_funcs += 1;
    }

    fn visit_const_decl(&mut self, _decl: &'ast ConstDecl<'ast>) {
        self.consts += 1;
    }

    fn visit_generic_struct_decl(&mut self, _decl: &'ast GenericStructDecl<'ast>) {
        self.generic_structs += 1;
    }

    fn visit_generic_impl_block(&mut self, _decl: &'ast GenericImplBlock<'ast>) {
        self.generic_impls += 1;
    }

    fn visit_type_param(&mut self, _param: &'ast TypeParam<'ast>) {
        self.type_params += 1;
    }

    fn visit_constraint(&mut self, _constraint: &'ast Constraint) {
        self.constraints += 1;
    }

    fn visit_block(&mut self, _block: &'ast Block<'ast>) {
        self.blocks += 1;
    }

    fn visit_expr(&mut self, _expr: &'ast Expr<'ast>) {
        self.exprs += 1;
    }

    fn visit_type_expr(&mut self, _ty: &'ast TypeExpr<'ast>) {
        self.types += 1;
    }
}

#[test]
fn visitor_walk_traverses_generic_nodes() {
    let arena = Bump::new();
    let ty_i32 = alloc_type(
        &arena,
        TypeExpr::Named(NamedTypeExpr {
            id: NodeId::new(1),
            span: span(0, 3),
            name: symbol(1),
        }),
    );
    let type_params = arena_slice(
        &arena,
        [TypeParam {
            id: NodeId::new(2),
            span: span(4, 14),
            name: symbol(2),
            constraints: arena_slice(
                &arena,
                [Constraint {
                    id: NodeId::new(3),
                    span: span(7, 14),
                    kind: ConstraintKind::Ordered,
                }],
            ),
        }],
    );
    let ident = alloc_expr(
        &arena,
        Expr::Ident(IdentExpr {
            id: NodeId::new(4),
            span: span(15, 16),
            name: symbol(3),
        }),
    );
    let block = alloc_block(
        &arena,
        Block {
            id: NodeId::new(5),
            span: span(17, 25),
            statements: arena_slice(
                &arena,
                [Stmt::Return(ReturnStmt {
                    id: NodeId::new(6),
                    span: span(18, 24),
                    value: Some(ident),
                })],
            ),
        },
    );
    let decl = Decl::GenericFunc(GenericFuncDecl {
        id: NodeId::new(7),
        span: span(0, 25),
        name: symbol(4),
        type_params,
        params: arena_slice(
            &arena,
            [Param {
                id: NodeId::new(8),
                span: span(26, 31),
                name: symbol(5),
                ty: ty_i32,
            }],
        ),
        return_type: Some(ty_i32),
        body: block,
    });

    let mut visitor = CountingVisitor::default();
    walk_decl(&mut visitor, &decl);

    assert_eq!(visitor.decls, 1);
    assert_eq!(visitor.generic_funcs, 1);
    assert_eq!(visitor.type_params, 1);
    assert_eq!(visitor.constraints, 1);
    assert_eq!(visitor.blocks, 1);
    assert!(visitor.exprs >= 1);
    assert!(visitor.types >= 2);
}

#[test]
fn display_formats_generic_and_const_declarations() {
    let arena = Bump::new();
    let ty_i32 = alloc_type(
        &arena,
        TypeExpr::Named(NamedTypeExpr {
            id: NodeId::new(1),
            span: span(0, 3),
            name: symbol(1),
        }),
    );
    let ty_vec = alloc_type(
        &arena,
        TypeExpr::Generic(GenericTypeExpr {
            id: NodeId::new(2),
            span: span(4, 14),
            name: symbol(2),
            args: arena_slice(&arena, [ty_i32]),
        }),
    );
    let type_params = arena_slice(
        &arena,
        [TypeParam {
            id: NodeId::new(3),
            span: span(15, 25),
            name: symbol(3),
            constraints: arena_slice(&arena, [numeric_constraint(4, 18, 25)]),
        }],
    );
    let body = alloc_block(
        &arena,
        Block {
            id: NodeId::new(5),
            span: span(26, 40),
            statements: arena_slice(
                &arena,
                [Stmt::Return(ReturnStmt {
                    id: NodeId::new(6),
                    span: span(27, 39),
                    value: Some(alloc_expr(
                        &arena,
                        Expr::Ident(IdentExpr {
                            id: NodeId::new(7),
                            span: span(34, 35),
                            name: symbol(4),
                        }),
                    )),
                })],
            ),
        },
    );

    let generic_func = GenericFuncDecl {
        id: NodeId::new(8),
        span: span(0, 40),
        name: symbol(5),
        type_params,
        params: arena_slice(
            &arena,
            [Param {
                id: NodeId::new(9),
                span: span(41, 50),
                name: symbol(4),
                ty: ty_i32,
            }],
        ),
        return_type: Some(ty_i32),
        body,
    };

    let generic_struct = GenericStructDecl {
        id: NodeId::new(10),
        span: span(51, 80),
        name: symbol(2),
        type_params,
        fields: arena_slice(
            &arena,
            [FieldDef {
                id: NodeId::new(11),
                span: span(60, 68),
                name: symbol(6),
                ty: ty_i32,
            }],
        ),
    };

    let const_decl = ConstDecl {
        id: NodeId::new(12),
        span: span(81, 95),
        name: symbol(7),
        type_ann: ty_vec,
        value: alloc_expr(
            &arena,
            Expr::Literal(LitExpr {
                id: NodeId::new(13),
                span: span(90, 95),
                literal: Literal::Str(symbol(8)),
            }),
        ),
    };

    let generic_impl = GenericImplBlock {
        id: NodeId::new(14),
        span: span(96, 130),
        target: symbol(2),
        type_params,
        methods: arena_slice(
            &arena,
            [FuncDecl {
                id: NodeId::new(15),
                span: span(100, 130),
                name: symbol(9),
                params: arena_slice(&arena, []),
                return_type: Some(ty_i32),
                body,
            }],
        ),
    };

    let rendered_func = generic_func.to_string();
    let rendered_struct = generic_struct.to_string();
    let rendered_const = const_decl.to_string();
    let rendered_impl = generic_impl.to_string();

    assert!(rendered_func.contains("func sym_5<sym_3: Numeric>(sym_4: sym_1) -> sym_1:"));
    assert!(rendered_struct.contains("struct sym_2<sym_3: Numeric>:"));
    assert!(rendered_const.contains("const sym_7: sym_2<sym_1> = \"str_8\""));
    assert!(rendered_impl.contains("impl sym_2<sym_3: Numeric>:"));
}
