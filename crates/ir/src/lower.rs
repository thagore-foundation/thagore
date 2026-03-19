//! AST to IR lowering for Thagore.

extern crate alloc;

use alloc::collections::BTreeMap;
use alloc::vec;
use alloc::vec::Vec;
use core::mem;

use thagore_ast::{
    BinOp as AstBinOp, BlockRef, Decl, Expr, ExprRef, FlowDecl, FuncDecl, GenericFuncDecl,
    InternedStr, LetDecl, Literal, NodeId, Span, Stmt, StructDecl, UnaryOp as AstUnaryOp,
};
use thagore_typeck::{
    FunctionType, MonomorphInstance, StructField, TypeArena, TypeId, TypeKind, TypeTable,
};

use crate::block::{BasicBlock, BlockId, Terminator};
use crate::error::LoweringError;
use crate::instr::{BinOp, Const, Instr, UnOp, Value};
use crate::module::{IrFunction, IrModule, IrStruct};
use crate::validate::validate_module;

/// Reserved intrinsic symbol used to materialize array lengths in lowered IR.
pub const ARRAY_LEN_INTRINSIC: InternedStr = InternedStr::new(u32::MAX - 1);

/// Salt applied when deriving synthetic compensation symbols from a flow name.
pub const FLOW_COMPENSATE_SALT: u32 = 0x0F00_0001;

const METHOD_SYMBOL_SALT: u32 = 0x1F00_0001;

/// Lexically scoped lowering environment mapping symbols to storage slots.
#[derive(Debug, Clone, Default)]
pub struct LoweringEnv {
    scopes: Vec<BTreeMap<InternedStr, Value>>,
}

impl LoweringEnv {
    /// Creates a new lowering environment with a single root scope.
    #[must_use]
    pub fn new() -> Self {
        Self {
            scopes: vec![BTreeMap::new()],
        }
    }

    /// Pushes a new lexical scope.
    pub fn push_scope(&mut self) {
        self.scopes.push(BTreeMap::new());
    }

    /// Pops the current lexical scope while preserving the root scope.
    pub fn pop_scope(&mut self) {
        if self.scopes.len() > 1 {
            self.scopes.pop();
        }
    }

    /// Inserts a binding in the current scope.
    pub fn insert(&mut self, name: InternedStr, value: Value) {
        if let Some(scope) = self.scopes.last_mut() {
            scope.insert(name, value);
        }
    }

    /// Returns the nearest visible binding for `name`.
    #[must_use]
    pub fn get(&self, name: &InternedStr) -> Option<Value> {
        self.scopes
            .iter()
            .rev()
            .find_map(|scope| scope.get(name).copied())
    }
}

/// AST to IR lowerer.
#[derive(Debug)]
pub struct IrLowerer<'a> {
    module_name: InternedStr,
    types: &'a TypeArena,
    table: &'a TypeTable,
    monomorphs: &'a [MonomorphInstance],
    module_consts: BTreeMap<InternedStr, Const>,
    errors: Vec<LoweringError>,
}

impl<'a> IrLowerer<'a> {
    /// Creates a new IR lowerer for a module.
    #[must_use]
    pub fn new(
        module_name: InternedStr,
        types: &'a TypeArena,
        table: &'a TypeTable,
        monomorphs: &'a [MonomorphInstance],
    ) -> Self {
        Self {
            module_name,
            types,
            table,
            monomorphs,
            module_consts: BTreeMap::new(),
            errors: Vec::new(),
        }
    }

    /// Lowers a full AST module to IR and validates the result.
    pub fn lower_module<'ast>(
        &mut self,
        decls: &'ast [Decl<'ast>],
    ) -> Result<IrModule, Vec<LoweringError>> {
        self.errors.clear();
        self.module_consts.clear();
        let mut monomorph_lookup = BTreeMap::new();
        for instance in self.monomorphs {
            monomorph_lookup.insert(
                (instance.generic_name, instance.result.type_id),
                instance.result.mangled_name,
            );
        }

        let mut module = IrModule::new(self.module_name);
        for decl in decls {
            if let Decl::Struct(struct_decl) = decl {
                module.structs.push(self.lower_struct_decl(struct_decl));
            }
        }

        for decl in decls {
            if let Decl::Const(const_decl) = decl {
                if let Some(value) = self.evaluate_const_expr(const_decl.value) {
                    let ty = self.node_type(const_decl.id, const_decl.span);
                    self.module_consts
                        .insert(const_decl.name, canonicalize_const(value, ty, self.types));
                } else {
                    self.errors.push(LoweringError::InvalidLoweringState {
                        message: "top-level const initializers must be compile-time constants",
                        span: const_decl.span,
                    });
                }
            }
        }

        for decl in decls {
            match decl {
                Decl::Struct(_) | Decl::Import(_) | Decl::Const(_) => {}
                Decl::Func(func_decl) => {
                    module
                        .functions
                        .push(self.lower_func_decl(func_decl, &monomorph_lookup))
                }
                Decl::GenericFunc(func_decl) => {
                    for instance in self
                        .monomorphs
                        .iter()
                        .filter(|instance| instance.generic_name == func_decl.name)
                    {
                        module.functions.push(self.lower_generic_func_decl(
                            func_decl,
                            instance,
                            &monomorph_lookup,
                        ));
                    }
                }
                Decl::GenericStruct(struct_decl) => {
                    self.errors.push(LoweringError::InvalidLoweringState {
                        message: "generic structs are not representable in IR yet",
                        span: struct_decl.span,
                    });
                }
                Decl::Extern(extern_decl) => {
                    let return_type =
                        self.node_type(extern_decl.return_type.id(), extern_decl.span);
                    let mut function = IrFunction::new(extern_decl.name, return_type);
                    function.is_extern = true;
                    for param in extern_decl.params {
                        let ty = self.node_type(param.id, param.span);
                        let value = Value::new(function.value_types.len() as u32);
                        function.value_types.push(ty);
                        function.params.push((value, ty));
                    }
                    module.functions.push(function);
                }
                Decl::Intent(intent_decl) => {
                    let mut builder = FunctionLowerer::new(
                        intent_decl.name,
                        self.types.unit(),
                        self.types,
                        self.table,
                        &monomorph_lookup,
                        &self.module_consts,
                        &mut self.errors,
                    );
                    for constraint in intent_decl.constraints {
                        builder.lower_expr(*constraint);
                    }
                    builder.lower_block(intent_decl.body);
                    module.functions.push(builder.finish());
                }
                Decl::Flow(flow_decl) => {
                    module
                        .functions
                        .extend(self.lower_flow_decl(flow_decl, &monomorph_lookup))
                }
                Decl::Impl(impl_block) => {
                    for method in impl_block.methods {
                        module
                            .functions
                            .push(self.lower_func_decl(method, &monomorph_lookup));
                    }
                }
                Decl::GenericImpl(impl_block) => {
                    self.errors.push(LoweringError::InvalidLoweringState {
                        message: "generic impl blocks are not representable in IR yet",
                        span: impl_block.span,
                    });
                }
                Decl::Let(let_decl) => {
                    self.errors.push(LoweringError::InvalidLoweringState {
                        message: "top-level let declarations are not representable in IR yet",
                        span: let_decl.span,
                    });
                }
            }
        }

        self.errors.extend(validate_module(&module));
        if self.errors.is_empty() {
            Ok(module)
        } else {
            Err(mem::take(&mut self.errors))
        }
    }

    fn lower_struct_decl<'ast>(&mut self, decl: &'ast StructDecl<'ast>) -> IrStruct {
        let mut fields = Vec::new();
        for field in decl.fields {
            let ty = self.node_type(field.id, field.span);
            fields.push((field.name, ty));
        }
        IrStruct {
            name: decl.name,
            fields,
        }
    }

    fn lower_func_decl<'ast>(
        &mut self,
        decl: &'ast FuncDecl<'ast>,
        monomorph_lookup: &BTreeMap<(InternedStr, TypeId), InternedStr>,
    ) -> IrFunction {
        let return_type = match self.types.kind(self.node_type(decl.id, decl.span)).clone() {
            TypeKind::Function(signature) => signature.return_type,
            _ => decl
                .return_type
                .map(|ty| self.node_type(ty.id(), ty.span()))
                .unwrap_or_else(|| self.types.unit()),
        };
        let param_types: Vec<(InternedStr, TypeId)> = decl
            .params
            .iter()
            .map(|param| (param.name, self.node_type(param.id, param.span)))
            .collect();
        let mut builder = FunctionLowerer::new(
            decl.name,
            return_type,
            self.types,
            self.table,
            monomorph_lookup,
            &self.module_consts,
            &mut self.errors,
        );
        for (name, ty) in param_types {
            builder.add_param(name, ty);
        }
        builder.lower_block(decl.body);
        builder.finish()
    }

    fn lower_generic_func_decl<'ast>(
        &mut self,
        decl: &'ast GenericFuncDecl<'ast>,
        instance: &MonomorphInstance,
        monomorph_lookup: &BTreeMap<(InternedStr, TypeId), InternedStr>,
    ) -> IrFunction {
        let return_type = match self.types.kind(instance.result.type_id).clone() {
            TypeKind::Function(signature) => signature.return_type,
            _ => self.types.unit(),
        };
        let param_types: Vec<(InternedStr, TypeId)> = decl
            .params
            .iter()
            .map(|param| {
                (
                    param.name,
                    instance
                        .table
                        .get(param.id)
                        .unwrap_or_else(|| self.types.unknown()),
                )
            })
            .collect();
        let mut builder = FunctionLowerer::new(
            instance.result.mangled_name,
            return_type,
            self.types,
            &instance.table,
            monomorph_lookup,
            &self.module_consts,
            &mut self.errors,
        );
        for (name, ty) in param_types {
            builder.add_param(name, ty);
        }
        builder.lower_block(decl.body);
        builder.finish()
    }

    fn lower_flow_decl<'ast>(
        &mut self,
        decl: &'ast FlowDecl<'ast>,
        monomorph_lookup: &BTreeMap<(InternedStr, TypeId), InternedStr>,
    ) -> Vec<IrFunction> {
        let mut builder = FunctionLowerer::new(
            decl.name,
            self.types.unit(),
            self.types,
            self.table,
            monomorph_lookup,
            &self.module_consts,
            &mut self.errors,
        );

        if decl.stages.is_empty() {
            builder.set_default_terminator();
        } else {
            let stage_blocks: Vec<BlockId> =
                decl.stages.iter().map(|_| builder.new_block()).collect();
            let exit_block = builder.new_block();
            builder.set_terminator(Terminator::Jump(stage_blocks[0]), decl.span);

            for (index, stage) in decl.stages.iter().enumerate() {
                builder.set_current_block(stage_blocks[index]);
                builder.lower_block(stage.body);
                if !builder.current_block_terminated() {
                    let target = stage_blocks.get(index + 1).copied().unwrap_or(exit_block);
                    builder.set_terminator(Terminator::Jump(target), stage.span);
                }
            }

            builder.set_current_block(exit_block);
            builder.set_default_terminator();
        }

        let mut functions = vec![builder.finish()];
        if let Some(compensation) = decl.compensation {
            let compensate_name = derive_symbol(decl.name, FLOW_COMPENSATE_SALT);
            let mut compensation_builder = FunctionLowerer::new(
                compensate_name,
                self.types.unit(),
                self.types,
                self.table,
                monomorph_lookup,
                &self.module_consts,
                &mut self.errors,
            );
            compensation_builder.lower_block(compensation);
            functions.push(compensation_builder.finish());
        }
        functions
    }

    fn node_type(&mut self, node: NodeId, span: Span) -> TypeId {
        self.table.get(node).unwrap_or_else(|| {
            self.errors.push(LoweringError::MissingType { node, span });
            self.types.unknown()
        })
    }

    fn evaluate_const_expr<'ast>(&mut self, expr: ExprRef<'ast>) -> Option<Const> {
        match expr {
            Expr::Literal(literal) => Some(match literal.literal {
                Literal::Int(value) => Const::Int(value),
                Literal::Float(value) => Const::Float(value),
                Literal::Bool(value) => Const::Bool(value),
                Literal::Str(value) => Const::Str(value),
            }),
            Expr::Ident(ident) => self.module_consts.get(&ident.name).cloned(),
            Expr::Unary(unary) => {
                let operand = self.evaluate_const_expr(unary.operand)?;
                match (unary.op, operand) {
                    (AstUnaryOp::Neg, Const::Int(value)) => Some(Const::Int(-value)),
                    (AstUnaryOp::Neg, Const::Float(value)) => Some(Const::Float(-value)),
                    (AstUnaryOp::Not, Const::Bool(value)) => Some(Const::Bool(!value)),
                    _ => None,
                }
            }
            Expr::Binary(binary) => self.evaluate_const_binary(binary),
            _ => None,
        }
    }

    fn evaluate_const_binary<'ast>(&mut self, expr: &'ast thagore_ast::BinaryExpr<'ast>) -> Option<Const> {
        let left = self.evaluate_const_expr(expr.left)?;
        let right = self.evaluate_const_expr(expr.right)?;
        match (expr.op, left, right) {
            (AstBinOp::Add, Const::Int(left), Const::Int(right)) => Some(Const::Int(left + right)),
            (AstBinOp::Sub, Const::Int(left), Const::Int(right)) => Some(Const::Int(left - right)),
            (AstBinOp::Mul, Const::Int(left), Const::Int(right)) => Some(Const::Int(left * right)),
            (AstBinOp::Div, Const::Int(left), Const::Int(right)) if right != 0 => {
                Some(Const::Int(left / right))
            }
            (AstBinOp::Rem, Const::Int(left), Const::Int(right)) if right != 0 => {
                Some(Const::Int(left % right))
            }
            (AstBinOp::Add, Const::Float(left), Const::Float(right)) => {
                Some(Const::Float(left + right))
            }
            (AstBinOp::Sub, Const::Float(left), Const::Float(right)) => {
                Some(Const::Float(left - right))
            }
            (AstBinOp::Mul, Const::Float(left), Const::Float(right)) => {
                Some(Const::Float(left * right))
            }
            (AstBinOp::Div, Const::Float(left), Const::Float(right)) => {
                Some(Const::Float(left / right))
            }
            (AstBinOp::Rem, Const::Float(left), Const::Float(right)) => {
                Some(Const::Float(left % right))
            }
            (AstBinOp::Eq, Const::Int(left), Const::Int(right)) => Some(Const::Bool(left == right)),
            (AstBinOp::NotEq, Const::Int(left), Const::Int(right)) => {
                Some(Const::Bool(left != right))
            }
            (AstBinOp::Lt, Const::Int(left), Const::Int(right)) => Some(Const::Bool(left < right)),
            (AstBinOp::LtEq, Const::Int(left), Const::Int(right)) => {
                Some(Const::Bool(left <= right))
            }
            (AstBinOp::Gt, Const::Int(left), Const::Int(right)) => Some(Const::Bool(left > right)),
            (AstBinOp::GtEq, Const::Int(left), Const::Int(right)) => {
                Some(Const::Bool(left >= right))
            }
            (AstBinOp::Eq, Const::Float(left), Const::Float(right)) => {
                Some(Const::Bool(left == right))
            }
            (AstBinOp::NotEq, Const::Float(left), Const::Float(right)) => {
                Some(Const::Bool(left != right))
            }
            (AstBinOp::Lt, Const::Float(left), Const::Float(right)) => {
                Some(Const::Bool(left < right))
            }
            (AstBinOp::LtEq, Const::Float(left), Const::Float(right)) => {
                Some(Const::Bool(left <= right))
            }
            (AstBinOp::Gt, Const::Float(left), Const::Float(right)) => {
                Some(Const::Bool(left > right))
            }
            (AstBinOp::GtEq, Const::Float(left), Const::Float(right)) => {
                Some(Const::Bool(left >= right))
            }
            (AstBinOp::Eq, Const::Bool(left), Const::Bool(right)) => Some(Const::Bool(left == right)),
            (AstBinOp::NotEq, Const::Bool(left), Const::Bool(right)) => {
                Some(Const::Bool(left != right))
            }
            (AstBinOp::And, Const::Bool(left), Const::Bool(right)) => {
                Some(Const::Bool(left && right))
            }
            (AstBinOp::Or, Const::Bool(left), Const::Bool(right)) => {
                Some(Const::Bool(left || right))
            }
            _ => None,
        }
    }
}

#[derive(Debug)]
struct FunctionLowerer<'a, 'b> {
    function: IrFunction,
    types: &'a TypeArena,
    table: &'a TypeTable,
    monomorph_lookup: &'a BTreeMap<(InternedStr, TypeId), InternedStr>,
    module_consts: &'a BTreeMap<InternedStr, Const>,
    errors: &'b mut Vec<LoweringError>,
    env: LoweringEnv,
    loop_targets: Vec<LoopTargets>,
    current_block: BlockId,
    next_block: u32,
}

#[derive(Debug, Clone, Copy)]
struct LoopTargets {
    break_block: BlockId,
    continue_block: BlockId,
}

impl<'a, 'b> FunctionLowerer<'a, 'b> {
    fn new(
        name: InternedStr,
        return_type: TypeId,
        types: &'a TypeArena,
        table: &'a TypeTable,
        monomorph_lookup: &'a BTreeMap<(InternedStr, TypeId), InternedStr>,
        module_consts: &'a BTreeMap<InternedStr, Const>,
        errors: &'b mut Vec<LoweringError>,
    ) -> Self {
        let entry = BlockId::new(0);
        let mut function = IrFunction::new(name, return_type);
        function.entry = entry;
        function.blocks.push(BasicBlock::new(entry));
        Self {
            function,
            types,
            table,
            monomorph_lookup,
            module_consts,
            errors,
            env: LoweringEnv::new(),
            loop_targets: Vec::new(),
            current_block: entry,
            next_block: 1,
        }
    }

    fn finish(mut self) -> IrFunction {
        self.set_default_terminator();
        self.function
    }

    fn add_param(&mut self, name: InternedStr, ty: TypeId) {
        let param_value = self.new_value(ty);
        self.function.params.push((param_value, ty));
        let slot = self.new_value(ty);
        self.emit(Instr::Alloc(slot, ty));
        self.emit(Instr::Store(slot, param_value));
        self.env.insert(name, slot);
    }

    fn lower_block<'ast>(&mut self, block: BlockRef<'ast>) {
        self.env.push_scope();
        for stmt in block.statements {
            self.ensure_open_block();
            self.lower_stmt(stmt);
        }
        self.env.pop_scope();
    }

    fn lower_stmt<'ast>(&mut self, stmt: &'ast Stmt<'ast>) {
        match stmt {
            Stmt::Let(node) => self.lower_let_decl(node),
            Stmt::Expr(node) => {
                self.lower_expr(node.expr);
            }
            Stmt::Return(node) => {
                let value = node.value.map(|expr| self.lower_expr(expr));
                self.set_terminator(Terminator::Return(value), node.span);
            }
            Stmt::If(node) => {
                self.lower_if(node.condition, node.then_block, node.else_block, node.span)
            }
            Stmt::While(node) => self.lower_while(node.condition, node.body, node.span),
            Stmt::For(node) => self.lower_for(node.binding, node.iterator, node.body, node.span),
            Stmt::Break(node) => {
                if let Some(targets) = self.loop_targets.last().copied() {
                    self.set_terminator(Terminator::Jump(targets.break_block), node.span);
                } else {
                    self.errors.push(LoweringError::InvalidLoweringState {
                        message: "break used outside of a loop",
                        span: node.span,
                    });
                    self.emit_unreachable(node.span);
                }
            }
            Stmt::Continue(node) => {
                if let Some(targets) = self.loop_targets.last().copied() {
                    self.set_terminator(Terminator::Jump(targets.continue_block), node.span);
                } else {
                    self.errors.push(LoweringError::InvalidLoweringState {
                        message: "continue used outside of a loop",
                        span: node.span,
                    });
                    self.emit_unreachable(node.span);
                }
            }
        }
    }

    fn lower_let_decl<'ast>(&mut self, decl: &'ast LetDecl<'ast>) {
        let ty = self.node_type(decl.id, decl.span);
        let init = self.lower_expr(decl.initializer);
        let slot = self.new_value(ty);
        self.emit(Instr::Alloc(slot, ty));
        self.emit(Instr::Store(slot, init));
        self.env.insert(decl.name, slot);
    }

    fn lower_if<'ast>(
        &mut self,
        condition: ExprRef<'ast>,
        then_block: BlockRef<'ast>,
        else_block: Option<BlockRef<'ast>>,
        span: Span,
    ) {
        let condition_value = self.lower_expr(condition);
        let then_id = self.new_block();
        let else_id = self.new_block();
        let merge_id = self.new_block();
        self.set_terminator(Terminator::Branch(condition_value, then_id, else_id), span);

        self.set_current_block(then_id);
        self.lower_block(then_block);
        if !self.current_block_terminated() {
            self.set_terminator(Terminator::Jump(merge_id), then_block.span);
        }

        self.set_current_block(else_id);
        if let Some(block) = else_block {
            self.lower_block(block);
            if !self.current_block_terminated() {
                self.set_terminator(Terminator::Jump(merge_id), block.span);
            }
        } else {
            self.set_terminator(Terminator::Jump(merge_id), span);
        }

        self.set_current_block(merge_id);
    }

    fn lower_while<'ast>(&mut self, condition: ExprRef<'ast>, body: BlockRef<'ast>, span: Span) {
        let header = self.new_block();
        let body_block = self.new_block();
        let exit_block = self.new_block();

        self.set_terminator(Terminator::Jump(header), span);

        self.set_current_block(header);
        let condition_value = self.lower_expr(condition);
        self.set_terminator(
            Terminator::Branch(condition_value, body_block, exit_block),
            condition.span(),
        );

        self.set_current_block(body_block);
        self.loop_targets.push(LoopTargets {
            break_block: exit_block,
            continue_block: header,
        });
        self.lower_block(body);
        self.loop_targets.pop();
        if !self.current_block_terminated() {
            self.set_terminator(Terminator::Jump(header), body.span);
        }

        self.set_current_block(exit_block);
    }

    fn lower_for<'ast>(
        &mut self,
        binding: InternedStr,
        iterator: ExprRef<'ast>,
        body: BlockRef<'ast>,
        span: Span,
    ) {
        let iter_value = self.lower_expr(iterator);
        let iter_ty = self.node_type(iterator.id(), iterator.span());
        let element_ty = match self.types.kind(iter_ty) {
            TypeKind::Array(element) => *element,
            TypeKind::Unknown => self.types.unknown(),
            other => {
                let found = match other {
                    TypeKind::Unit => self.types.unit(),
                    TypeKind::Unknown => self.types.unknown(),
                    TypeKind::I32 => self.types.i32(),
                    TypeKind::I64 => self.types.i64(),
                    TypeKind::F64 => self.types.f64(),
                    TypeKind::Bool => self.types.bool(),
                    TypeKind::Str => self.types.str(),
                    TypeKind::Struct(_) => iter_ty,
                    TypeKind::Array(element) => *element,
                    TypeKind::Function(FunctionType { return_type, .. }) => *return_type,
                    TypeKind::Infer(_) | TypeKind::IntInfer(_) => self.types.unknown(),
                };
                self.errors.push(LoweringError::NotIndexable {
                    found,
                    span: iterator.span(),
                });
                self.emit_unreachable(span);
                self.types.unknown()
            }
        };

        let len_value = self.new_value(self.types.i32());
        self.emit(Instr::Call(
            len_value,
            ARRAY_LEN_INTRINSIC,
            vec![iter_value],
        ));
        let binding_slot = self.new_value(element_ty);
        self.emit(Instr::Alloc(binding_slot, element_ty));
        let zero = self.emit_const(Const::Int(0), self.types.i32());

        let header = self.new_block();
        let body_block = self.new_block();
        let continue_block = self.new_block();
        let exit_block = self.new_block();
        let preheader = self.current_block;
        self.set_terminator(Terminator::Jump(header), span);

        self.set_current_block(header);
        let index_value = self.new_value(self.types.i32());
        let phi_index = self.emit_phi(index_value, vec![(zero, preheader)]);
        let cond_value = self.new_value(self.types.bool());
        self.emit(Instr::BinOp(cond_value, BinOp::Lt, phi_index, len_value));
        self.set_terminator(Terminator::Branch(cond_value, body_block, exit_block), span);

        self.set_current_block(body_block);
        let element_value = self.new_value(element_ty);
        self.emit(Instr::Index(element_value, iter_value, phi_index));
        self.emit(Instr::Store(binding_slot, element_value));
        self.env.push_scope();
        self.env.insert(binding, binding_slot);
        self.loop_targets.push(LoopTargets {
            break_block: exit_block,
            continue_block,
        });
        self.lower_block(body);
        self.loop_targets.pop();
        self.env.pop_scope();
        if !self.current_block_terminated() {
            self.set_terminator(Terminator::Jump(continue_block), body.span);
        }

        self.set_current_block(continue_block);
        let one = self.emit_const(Const::Int(1), self.types.i32());
        let next_index = self.new_value(self.types.i32());
        self.emit(Instr::BinOp(next_index, BinOp::Add, phi_index, one));
        self.set_terminator(Terminator::Jump(header), body.span);
        self.patch_phi_input(header, index_value, next_index, continue_block);
        self.set_current_block(exit_block);
    }

    fn lower_expr<'ast>(&mut self, expr: ExprRef<'ast>) -> Value {
        match expr {
            Expr::Literal(literal) => {
                let ty = self.node_type(literal.id, literal.span);
                let constant = match literal.literal {
                    Literal::Int(value) => Const::Int(value),
                    Literal::Float(value) => Const::Float(value),
                    Literal::Bool(value) => Const::Bool(value),
                    Literal::Str(value) => Const::Str(value),
                };
                self.emit_const(canonicalize_const(constant, ty, self.types), ty)
            }
            Expr::Ident(ident) => {
                let ty = self.node_type(ident.id, ident.span);
                if let Some(constant) = self.module_consts.get(&ident.name).cloned() {
                    return self.emit_const(canonicalize_const(constant, ty, self.types), ty);
                }
                let Some(slot) = self.env.get(&ident.name) else {
                    self.errors.push(LoweringError::UnknownIdentifier {
                        name: ident.name,
                        span: ident.span,
                    });
                    return self.emit_placeholder(ty);
                };
                let value = self.new_value(ty);
                self.emit(Instr::Load(value, slot));
                value
            }
            Expr::Binary(binary) => {
                let left = self.lower_expr(binary.left);
                let right = self.lower_expr(binary.right);
                let ty = self.node_type(binary.id, binary.span);
                let value = self.new_value(ty);
                self.emit(Instr::BinOp(value, lower_bin_op(binary.op), left, right));
                value
            }
            Expr::Unary(unary) => {
                let operand = self.lower_expr(unary.operand);
                let ty = self.node_type(unary.id, unary.span);
                let value = self.new_value(ty);
                self.emit(Instr::UnOp(value, lower_un_op(unary.op), operand));
                value
            }
            Expr::Call(call) => self.lower_call_expr(call),
            Expr::FieldAccess(field) => {
                let base = self.lower_expr(field.object);
                let ty = self.node_type(field.id, field.span);
                let object_ty = self.node_type(field.object.id(), field.object.span());
                if self.lookup_field_type(object_ty, field.field).is_none() {
                    self.errors.push(LoweringError::UnknownField {
                        struct_name: self.struct_name_for_type(object_ty).unwrap_or(field.field),
                        field: field.field,
                        span: field.span,
                    });
                    return self.emit_placeholder(ty);
                }
                let value = self.new_value(ty);
                self.emit(Instr::GetField(value, base, field.field));
                value
            }
            Expr::Index(index) => {
                let object = self.lower_expr(index.object);
                let idx = self.lower_expr(index.index);
                let ty = self.node_type(index.id, index.span);
                let value = self.new_value(ty);
                self.emit(Instr::Index(value, object, idx));
                value
            }
            Expr::Assign(assign) => {
                let value = self.lower_expr(assign.value);
                if !self.lower_assignment_target(assign.target, value, assign.span) {
                    self.errors
                        .push(LoweringError::InvalidAssignmentTarget { span: assign.span });
                    self.emit_unreachable(assign.span);
                }
                value
            }
        }
    }

    fn lower_call_expr<'ast>(&mut self, call: &'ast thagore_ast::CallExpr<'ast>) -> Value {
        let result_ty = self.node_type(call.id, call.span);
        let mut args = Vec::new();
        let callee_name = match call.callee {
            Expr::Ident(ident) => self
                .monomorph_lookup
                .get(&(ident.name, self.node_type(ident.id, ident.span)))
                .copied()
                .unwrap_or(ident.name),
            Expr::FieldAccess(field) => {
                let object = self.lower_expr(field.object);
                args.push(object);
                let object_ty = self.node_type(field.object.id(), field.object.span());
                let Some(struct_name) = self.struct_name_for_type(object_ty) else {
                    self.errors.push(LoweringError::NotCallable {
                        found: object_ty,
                        span: field.span,
                    });
                    return self.emit_placeholder(result_ty);
                };
                derive_symbol_pair(struct_name, field.field, METHOD_SYMBOL_SALT)
            }
            other => {
                let found = self.node_type(other.id(), other.span());
                self.errors.push(LoweringError::NotCallable {
                    found,
                    span: other.span(),
                });
                return self.emit_placeholder(result_ty);
            }
        };

        for arg in call.args {
            args.push(self.lower_expr(arg));
        }
        let value = self.new_value(result_ty);
        self.emit(Instr::Call(value, callee_name, args));
        value
    }

    fn lower_assignment_target<'ast>(
        &mut self,
        target: ExprRef<'ast>,
        value: Value,
        span: Span,
    ) -> bool {
        match target {
            Expr::Ident(ident) => {
                if let Some(slot) = self.env.get(&ident.name) {
                    self.emit(Instr::Store(slot, value));
                    true
                } else {
                    self.errors.push(LoweringError::UnknownIdentifier {
                        name: ident.name,
                        span: ident.span,
                    });
                    false
                }
            }
            Expr::FieldAccess(field) => {
                let Expr::Ident(base) = field.object else {
                    return false;
                };
                let Some(slot) = self.env.get(&base.name) else {
                    self.errors.push(LoweringError::UnknownIdentifier {
                        name: base.name,
                        span: base.span,
                    });
                    return false;
                };
                let base_ty = self.node_type(field.object.id(), field.object.span());
                if self.lookup_field_type(base_ty, field.field).is_none() {
                    self.errors.push(LoweringError::UnknownField {
                        struct_name: self.struct_name_for_type(base_ty).unwrap_or(base.name),
                        field: field.field,
                        span,
                    });
                    return false;
                }
                self.emit(Instr::SetField(slot, field.field, value));
                true
            }
            _ => false,
        }
    }

    fn lookup_field_type(&self, type_id: TypeId, field: InternedStr) -> Option<TypeId> {
        match self.types.kind(type_id) {
            TypeKind::Struct(struct_ty) => struct_ty
                .fields
                .iter()
                .find(|candidate: &&StructField| candidate.name == field)
                .map(|candidate| candidate.ty),
            _ => None,
        }
    }

    fn struct_name_for_type(&self, type_id: TypeId) -> Option<InternedStr> {
        match self.types.kind(type_id) {
            TypeKind::Struct(struct_ty) => Some(struct_ty.name),
            _ => None,
        }
    }

    fn node_type(&mut self, node: NodeId, span: Span) -> TypeId {
        self.table.get(node).unwrap_or_else(|| {
            self.errors.push(LoweringError::MissingType { node, span });
            self.types.unknown()
        })
    }

    fn emit(&mut self, instr: Instr) {
        if let Some(block) = self.function.block_mut(self.current_block) {
            block.instructions.push(instr);
        }
    }

    fn emit_const(&mut self, constant: Const, ty: TypeId) -> Value {
        let value = self.new_value(ty);
        self.emit(Instr::Const(value, constant));
        value
    }

    fn emit_placeholder(&mut self, ty: TypeId) -> Value {
        self.emit_const(Const::Unit, ty)
    }

    fn emit_unreachable(&mut self, span: Span) {
        if !self.current_block_terminated() {
            self.set_terminator(Terminator::Unreachable, span);
        }
    }

    fn emit_phi(&mut self, value: Value, inputs: Vec<(Value, BlockId)>) -> Value {
        self.emit(Instr::Phi(value, inputs));
        value
    }

    fn patch_phi_input(
        &mut self,
        block: BlockId,
        value: Value,
        incoming: Value,
        predecessor: BlockId,
    ) {
        if let Some(block_ref) = self.function.block_mut(block) {
            for instr in &mut block_ref.instructions {
                if let Instr::Phi(phi_value, inputs) = instr {
                    if *phi_value == value {
                        inputs.push((incoming, predecessor));
                        return;
                    }
                }
            }
        }
    }

    fn new_value(&mut self, ty: TypeId) -> Value {
        let value = Value::new(self.function.value_types.len() as u32);
        self.function.value_types.push(ty);
        value
    }

    fn new_block(&mut self) -> BlockId {
        let id = BlockId::new(self.next_block);
        self.next_block += 1;
        self.function.blocks.push(BasicBlock::new(id));
        id
    }

    fn set_current_block(&mut self, block: BlockId) {
        self.current_block = block;
    }

    fn current_block_terminated(&self) -> bool {
        self.function
            .block(self.current_block)
            .map(BasicBlock::is_terminated)
            .unwrap_or(false)
    }

    fn ensure_open_block(&mut self) {
        if self.current_block_terminated() {
            self.current_block = self.new_block();
        }
    }

    fn set_terminator(&mut self, terminator: Terminator, span: Span) {
        let current = self.current_block;
        if let Some(block) = self.function.block_mut(current) {
            if block.terminator.is_some() {
                self.errors.push(LoweringError::DuplicateTerminator {
                    block: current.as_u32(),
                });
                return;
            }

            let successors = match &terminator {
                Terminator::Jump(target) => vec![*target],
                Terminator::Branch(_, then_block, else_block) => vec![*then_block, *else_block],
                Terminator::Return(_) | Terminator::Unreachable => Vec::new(),
            };

            block.successors = successors.clone();
            block.terminator = Some(terminator);
            for successor in successors {
                if let Some(target) = self.function.block_mut(successor) {
                    if !target.predecessors.contains(&current) {
                        target.predecessors.push(current);
                    }
                } else {
                    self.errors.push(LoweringError::InvalidBlockEdge {
                        from: current.as_u32(),
                        to: successor.as_u32(),
                    });
                }
            }
        } else {
            self.errors.push(LoweringError::InvalidLoweringState {
                message: "attempted to terminate a missing block",
                span,
            });
        }
    }

    fn set_default_terminator(&mut self) {
        if self.current_block_terminated() {
            return;
        }
        let terminator = if self.function.return_type == self.types.unit() {
            Terminator::Return(None)
        } else {
            Terminator::Unreachable
        };
        self.set_terminator(terminator, Span::empty());
    }
}

fn lower_bin_op(op: AstBinOp) -> BinOp {
    match op {
        AstBinOp::Or => BinOp::Or,
        AstBinOp::And => BinOp::And,
        AstBinOp::Add => BinOp::Add,
        AstBinOp::Sub => BinOp::Sub,
        AstBinOp::Mul => BinOp::Mul,
        AstBinOp::Div => BinOp::Div,
        AstBinOp::Rem => BinOp::Rem,
        AstBinOp::Eq => BinOp::Eq,
        AstBinOp::NotEq => BinOp::NotEq,
        AstBinOp::Lt => BinOp::Lt,
        AstBinOp::LtEq => BinOp::LtEq,
        AstBinOp::Gt => BinOp::Gt,
        AstBinOp::GtEq => BinOp::GtEq,
    }
}

fn lower_un_op(op: AstUnaryOp) -> UnOp {
    match op {
        AstUnaryOp::Plus => UnOp::Plus,
        AstUnaryOp::Neg => UnOp::Neg,
        AstUnaryOp::Not => UnOp::Not,
    }
}

fn canonicalize_const(constant: Const, ty: TypeId, types: &TypeArena) -> Const {
    match (constant, types.kind(ty)) {
        (Const::Int(value), TypeKind::F64) => Const::Float(value as f64),
        (Const::Int(value), TypeKind::Bool) => Const::Bool(value != 0),
        (other, _) => other,
    }
}

fn derive_symbol(base: InternedStr, salt: u32) -> InternedStr {
    InternedStr::new(base.as_u32().wrapping_mul(16_777_619).wrapping_add(salt))
}

fn derive_symbol_pair(left: InternedStr, right: InternedStr, salt: u32) -> InternedStr {
    InternedStr::new(
        left.as_u32()
            .wrapping_mul(16_777_619)
            .wrapping_add(right.as_u32().rotate_left(13))
            .wrapping_add(salt),
    )
}
