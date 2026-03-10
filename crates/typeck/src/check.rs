//! Main type-checking pass for Thagore ASTs.

extern crate alloc;

use alloc::string::{String, ToString};
use alloc::vec::Vec;
use core::mem;
use thagore_ast::visitor::Visitor;
use thagore_ast::{
    AssignExpr, BinOp, BinaryExpr, BlockRef, CallExpr, ConstDecl, Decl, DeclRef, Expr, ExprRef,
    ExprStmt, ExternDecl, FieldAccessExpr, FieldDef, FlowDecl, FlowStage, ForStmt, FuncDecl,
    GenericFuncDecl, GenericTypeExpr, IdentExpr, IfStmt, ImportDecl, IndexExpr, InferTypeExpr,
    IntentDecl, LetDecl, LitExpr, Literal, NamedTypeExpr, Param, ReturnStmt, Stmt, StmtRef,
    StructDecl, TypeExpr, TypeExprRef, UnaryExpr, UnaryOp, WhileStmt,
};

use crate::error::TypeError;
use crate::func_check::{ResolvedReturnType, infer_function_return_type};
use crate::generics::{
    GenericFunctionTemplate, GenericParamSpec, MonomorphInstance, MonomorphRequest,
    MonomorphResult, MonomorphWorkList, TemplateType, check_constraint, mangle_type_args,
};
use crate::infer::InferenceSolver;
use crate::return_infer::{block_guarantees_return, collect_return_sites};
use crate::scope::{FnvBuildHasher, ScopeMap, ScopeStack};
use crate::table::TypeTable;
use crate::types::{StructField, TypeArena, TypeId, TypeKind};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub(crate) struct MethodKey {
    struct_name: thagore_ast::InternedStr,
    method_name: thagore_ast::InternedStr,
}

type SymbolNames = ScopeMap<thagore_ast::InternedStr, String>;
type StructTypes = ScopeMap<thagore_ast::InternedStr, TypeId>;
type MethodTypes = ScopeMap<MethodKey, TypeId>;

/// Two-pass Thagore type checker.
#[derive(Debug, Clone)]
pub struct TypeChecker {
    types: TypeArena,
    scopes: ScopeStack<thagore_ast::InternedStr, TypeId>,
    table: TypeTable,
    infer: InferenceSolver,
    errors: Vec<TypeError>,
    symbol_names: SymbolNames,
    struct_types: StructTypes,
    method_types: MethodTypes,
    generic_functions: ScopeMap<thagore_ast::InternedStr, GenericFunctionTemplate>,
    monomorphs: MonomorphWorkList,
    monomorph_instances: Vec<MonomorphInstance>,
    current_return_types: Vec<TypeId>,
    current_impl_targets: Vec<TypeId>,
    synthetic_symbol_cursor: u32,
}

impl Default for TypeChecker {
    fn default() -> Self {
        Self::new()
    }
}

impl TypeChecker {
    /// Creates a new type checker.
    #[must_use]
    pub fn new() -> Self {
        Self {
            types: TypeArena::new(),
            scopes: ScopeStack::new(),
            table: TypeTable::new(),
            infer: InferenceSolver::new(),
            errors: Vec::new(),
            symbol_names: ScopeMap::with_hasher(FnvBuildHasher::default()),
            struct_types: ScopeMap::with_hasher(FnvBuildHasher::default()),
            method_types: ScopeMap::with_hasher(FnvBuildHasher::default()),
            generic_functions: ScopeMap::with_hasher(FnvBuildHasher::default()),
            monomorphs: MonomorphWorkList::new(),
            monomorph_instances: Vec::new(),
            current_return_types: Vec::new(),
            current_impl_targets: Vec::new(),
            synthetic_symbol_cursor: 1_000_000,
        }
    }

    /// Registers a printable name for an interned symbol.
    pub fn register_symbol_name(&mut self, symbol: thagore_ast::InternedStr, name: &str) {
        self.symbol_names.insert(symbol, name.to_string());
    }

    /// Returns the current type arena.
    #[must_use]
    pub fn types(&self) -> &TypeArena {
        &self.types
    }

    /// Returns the deduplicated generic instantiations discovered so far.
    #[must_use]
    pub fn monomorphs(&self) -> &MonomorphWorkList {
        &self.monomorphs
    }

    /// Returns the fully type-checked generic function instantiations.
    #[must_use]
    pub fn monomorph_instances(&self) -> &[MonomorphInstance] {
        &self.monomorph_instances
    }

    /// Returns the printable name registered for `symbol`, if any.
    #[must_use]
    pub fn resolve_symbol_name(&self, symbol: thagore_ast::InternedStr) -> Option<&str> {
        self.symbol_name(symbol)
    }

    /// Type-checks a program and returns the resulting side table on success.
    pub fn check<'ast>(&mut self, decls: &'ast [Decl<'ast>]) -> Result<TypeTable, Vec<TypeError>> {
        self.reset_state();
        self.collect_top_level(decls);

        for decl in decls {
            self.visit_decl(decl);
        }

        self.check_pending_monomorphs(decls);
        self.finalize_inferred_function_signatures(decls);

        if self.errors.is_empty() {
            self.finalize_table_inferences();
            Ok(mem::take(&mut self.table))
        } else {
            Err(mem::take(&mut self.errors))
        }
    }

    fn reset_state(&mut self) {
        self.types.clear();
        self.scopes.clear();
        self.table.clear();
        self.infer.clear();
        self.errors.clear();
        self.struct_types.clear();
        self.method_types.clear();
        self.generic_functions.clear();
        self.monomorphs.clear();
        self.monomorph_instances.clear();
        self.current_return_types.clear();
        self.current_impl_targets.clear();
        self.synthetic_symbol_cursor = 1_000_000;
    }

    fn collect_top_level<'ast>(&mut self, decls: &'ast [Decl<'ast>]) {
        for decl in decls {
            if let Decl::Struct(struct_decl) = decl {
                let type_id = self.types.reserve_struct(struct_decl.name);
                self.struct_types.insert(struct_decl.name, type_id);
                self.table.insert(struct_decl.id, type_id);
            }
        }

        for decl in decls {
            match decl {
                Decl::Struct(struct_decl) => self.collect_struct_shape(struct_decl),
                Decl::Func(func_decl) => {
                    let signature = self.collect_function_signature(func_decl, None);
                    self.scopes.insert(func_decl.name, signature);
                    self.table.insert(func_decl.id, signature);
                }
                Decl::GenericFunc(func_decl) => {
                    let template = self.extract_generic_function_template(func_decl);
                    self.generic_functions.insert(func_decl.name, template);
                    self.table.insert(func_decl.id, self.types.unit());
                }
                Decl::Const(const_decl) => {
                    let ty = self.resolve_type_expr(const_decl.type_ann);
                    self.table.insert(const_decl.id, ty);
                }
                Decl::Extern(extern_decl) => {
                    let signature = self.collect_extern_signature(extern_decl);
                    self.scopes.insert(extern_decl.name, signature);
                    self.table.insert(extern_decl.id, signature);
                }
                Decl::Impl(impl_block) => self.collect_impl_signatures(impl_block),
                Decl::GenericStruct(struct_decl) => {
                    self.table.insert(struct_decl.id, self.types.unit());
                }
                Decl::GenericImpl(impl_block) => {
                    self.table.insert(impl_block.id, self.types.unit());
                }
                Decl::Import(import_decl) => {
                    self.table.insert(import_decl.id, self.types.unit());
                }
                Decl::Intent(intent_decl) => {
                    self.table.insert(intent_decl.id, self.types.unit());
                }
                Decl::Flow(flow_decl) => {
                    self.table.insert(flow_decl.id, self.types.unit());
                }
                Decl::Let(_) => {}
            }
        }
    }

    fn extract_generic_function_template<'ast>(
        &self,
        decl: &'ast GenericFuncDecl<'ast>,
    ) -> GenericFunctionTemplate {
        let type_param_names: Vec<_> = decl.type_params.iter().map(|param| param.name).collect();
        let type_params = decl
            .type_params
            .iter()
            .map(|param| GenericParamSpec {
                name: param.name,
                constraints: param
                    .constraints
                    .iter()
                    .map(|constraint| constraint.kind)
                    .collect(),
            })
            .collect();
        let params = decl
            .params
            .iter()
            .map(|param| self.template_type_from_expr(param.ty, &type_param_names))
            .collect();
        let return_type = decl
            .return_type
            .map(|ty| self.template_type_from_expr(ty, &type_param_names))
            .unwrap_or(TemplateType::Unit);

        GenericFunctionTemplate {
            name: decl.name,
            type_params,
            params,
            return_type,
        }
    }

    fn template_type_from_expr<'ast>(
        &self,
        ty: TypeExprRef<'ast>,
        type_params: &[thagore_ast::InternedStr],
    ) -> TemplateType {
        match ty {
            TypeExpr::Named(node) => {
                if type_params.contains(&node.name) {
                    TemplateType::TypeParam(node.name)
                } else {
                    TemplateType::Named(node.name)
                }
            }
            TypeExpr::Generic(node) => TemplateType::Generic {
                name: node.name,
                args: node
                    .args
                    .iter()
                    .map(|arg| self.template_type_from_expr(*arg, type_params))
                    .collect(),
            },
            TypeExpr::Infer(_) => TemplateType::Infer,
        }
    }

    fn collect_struct_shape<'ast>(&mut self, decl: &'ast StructDecl<'ast>) {
        let struct_id = self
            .struct_types
            .get(&decl.name)
            .copied()
            .unwrap_or_else(|| self.types.reserve_struct(decl.name));

        let mut fields = Vec::new();
        for field in decl.fields {
            let ty = self.resolve_type_expr(field.ty);
            self.table.insert(field.id, ty);
            fields.push(StructField {
                name: field.name,
                ty,
            });
        }
        self.types.set_struct_fields(struct_id, fields);
    }

    fn collect_function_signature<'ast>(
        &mut self,
        decl: &'ast FuncDecl<'ast>,
        impl_target: Option<TypeId>,
    ) -> TypeId {
        let mut params = Vec::new();
        for (index, param) in decl.params.iter().enumerate() {
            let is_self_param = index == 0 && self.symbol_name(param.name) == Some("self");
            let ty = if is_self_param {
                impl_target.unwrap_or_else(|| self.resolve_type_expr(param.ty))
            } else {
                self.resolve_type_expr(param.ty)
            };
            self.table.insert(param.id, ty);
            params.push(ty);
        }
        let return_type = decl
            .return_type
            .map(|ty| self.resolve_type_expr(ty))
            .unwrap_or_else(|| {
                let infer = self.types.fresh_infer();
                self.infer.sync_with_arena(&self.types);
                infer
            });
        self.types.intern_function(params, return_type)
    }

    fn collect_extern_signature<'ast>(&mut self, decl: &'ast ExternDecl<'ast>) -> TypeId {
        let mut params = Vec::new();
        for param in decl.params {
            let ty = self.resolve_type_expr(param.ty);
            self.table.insert(param.id, ty);
            params.push(ty);
        }
        let return_type = self.resolve_type_expr(decl.return_type);
        self.types.intern_function(params, return_type)
    }

    fn collect_impl_signatures<'ast>(&mut self, decl: &'ast thagore_ast::ImplBlock<'ast>) {
        let Some(target_type) = self.struct_types.get(&decl.target).copied() else {
            self.errors.push(TypeError::unknown(decl.span));
            return;
        };

        self.table.insert(decl.id, target_type);
        for method in decl.methods {
            let method_type = self.collect_function_signature(method, Some(target_type));
            self.method_types.insert(
                MethodKey {
                    struct_name: decl.target,
                    method_name: method.name,
                },
                method_type,
            );
            self.table.insert(method.id, method_type);
        }
    }

    fn resolve_type_expr<'ast>(&mut self, ty: TypeExprRef<'ast>) -> TypeId {
        match ty {
            TypeExpr::Named(node) => self.resolve_named_type(node),
            TypeExpr::Generic(node) => self.resolve_generic_type(node),
            TypeExpr::Infer(node) => {
                let infer = self.types.fresh_infer();
                self.table.insert(node.id, infer);
                self.infer.sync_with_arena(&self.types);
                infer
            }
        }
    }

    fn resolve_named_type(&mut self, ty: &NamedTypeExpr) -> TypeId {
        let resolved = self.resolve_named_type_symbol(ty.name, ty.span);
        self.table.insert(ty.id, resolved);
        resolved
    }

    fn resolve_named_type_symbol(
        &mut self,
        name: thagore_ast::InternedStr,
        span: thagore_ast::Span,
    ) -> TypeId {
        match self.symbol_name(name) {
            Some("i32") => self.types.i32(),
            Some("i64") => self.types.i64(),
            Some("f64") => self.types.f64(),
            Some("bool") => self.types.bool(),
            Some("str") | Some("ptr") => self.types.str(),
            Some("()") | Some("void") => self.types.unit(),
            _ => self.struct_types.get(&name).copied().unwrap_or_else(|| {
                self.errors.push(TypeError::unknown(span));
                self.types.unknown()
            }),
        }
    }

    fn resolve_generic_type<'ast>(&mut self, ty: &'ast GenericTypeExpr<'ast>) -> TypeId {
        let args: Vec<TypeId> = ty
            .args
            .iter()
            .map(|arg| self.resolve_type_expr(*arg))
            .collect();
        let resolved = match (self.symbol_name(ty.name), args.as_slice()) {
            (Some("Array"), [element]) | (Some("array"), [element]) => {
                self.types.intern_array(*element)
            }
            _ => {
                self.errors.push(TypeError::unknown(ty.span));
                self.types.unknown()
            }
        };
        self.table.insert(ty.id, resolved);
        resolved
    }

    fn symbol_name(&self, symbol: thagore_ast::InternedStr) -> Option<&str> {
        self.symbol_names.get(&symbol).map(String::as_str)
    }

    fn intern_synthetic_name(&mut self, name: String) -> thagore_ast::InternedStr {
        if let Some((symbol, _)) = self
            .symbol_names
            .iter()
            .find(|(_, existing)| existing.as_str() == name.as_str())
        {
            return *symbol;
        }

        let symbol = thagore_ast::InternedStr::new(self.synthetic_symbol_cursor);
        self.synthetic_symbol_cursor = self.synthetic_symbol_cursor.saturating_add(1);
        self.symbol_names.insert(symbol, name);
        symbol
    }

    fn instantiate_template_type(
        &mut self,
        template: &TemplateType,
        bindings: &[(thagore_ast::InternedStr, TypeId)],
    ) -> TypeId {
        match template {
            TemplateType::Unit => self.types.unit(),
            TemplateType::Named(name) => {
                self.resolve_named_type_symbol(*name, thagore_ast::Span::empty())
            }
            TemplateType::TypeParam(name) => bindings
                .iter()
                .find_map(|(binding_name, ty)| (*binding_name == *name).then_some(*ty))
                .unwrap_or_else(|| self.types.unknown()),
            TemplateType::Generic { name, args } => {
                let instantiated_args: Vec<_> = args
                    .iter()
                    .map(|arg| self.instantiate_template_type(arg, bindings))
                    .collect();
                match (self.symbol_name(*name), instantiated_args.as_slice()) {
                    (Some("Array"), [element]) | (Some("array"), [element]) => {
                        self.types.intern_array(*element)
                    }
                    _ => self.types.unknown(),
                }
            }
            TemplateType::Infer => {
                let infer = self.types.fresh_infer();
                self.infer.sync_with_arena(&self.types);
                infer
            }
        }
    }

    fn instantiate_generic_function<'ast>(
        &mut self,
        template: &GenericFunctionTemplate,
        args: &'ast [ExprRef<'ast>],
        span: thagore_ast::Span,
    ) -> Option<(TypeId, TypeId)> {
        if template.params.len() != args.len() {
            self.errors.push(TypeError::ArgumentCountMismatch {
                expected: template.params.len(),
                found: args.len(),
                span,
            });
        }

        let mut bindings = Vec::new();
        for type_param in &template.type_params {
            let infer = self.types.fresh_infer();
            self.infer.sync_with_arena(&self.types);
            bindings.push((type_param.name, infer));
        }

        for (expected_template, arg) in template.params.iter().zip(args.iter()) {
            let expected = self.instantiate_template_type(expected_template, &bindings);
            let found = self.check_expr(*arg);
            self.unify(expected, found, arg.span());
        }

        let mut concrete_args = Vec::new();
        for type_param in &template.type_params {
            let concrete = bindings
                .iter()
                .find_map(|(name, ty)| (*name == type_param.name).then_some(*ty))
                .map(|ty| self.default_inferred_type(ty))
                .unwrap_or_else(|| self.types.unknown());
            if self.types.is_unknown(concrete) || self.types.is_infer(concrete) {
                self.errors.push(TypeError::InferenceFailure { span });
                return None;
            }
            for constraint in &type_param.constraints {
                if !check_constraint(&self.types, concrete, *constraint) {
                    self.errors.push(TypeError::TypeMismatch {
                        expected: self.types.i32(),
                        found: concrete,
                        span,
                    });
                    return None;
                }
            }
            concrete_args.push(concrete);
        }

        let concrete_bindings = template
            .type_params
            .iter()
            .zip(concrete_args.iter())
            .map(|(param, ty)| (param.name, *ty))
            .collect::<Vec<_>>();
        let signature_params = template
            .params
            .iter()
            .map(|param| {
                let instantiated = self.instantiate_template_type(param, &concrete_bindings);
                self.default_inferred_type(instantiated)
            })
            .collect();
        let instantiated_return =
            self.instantiate_template_type(&template.return_type, &concrete_bindings);
        let return_type = self.default_inferred_type(instantiated_return);
        let signature = self.types.intern_function(signature_params, return_type);
        let mangled_suffix = mangle_type_args(
            &self.types,
            &|symbol| self.symbol_name(symbol).map(ToString::to_string),
            &concrete_args,
        );
        let mut mangled_name = String::from("__thagore_");
        mangled_name.push_str(self.symbol_name(template.name).unwrap_or("generic"));
        if !mangled_suffix.is_empty() {
            mangled_name.push('_');
            mangled_name.push_str(&mangled_suffix);
        }
        let mangled_symbol = self.intern_synthetic_name(mangled_name);
        let request = MonomorphRequest {
            generic_name: template.name,
            type_args: concrete_args,
            call_span: span,
        };
        self.monomorphs.record(
            request,
            MonomorphResult {
                mangled_name: mangled_symbol,
                type_id: signature,
            },
        );
        Some((signature, return_type))
    }

    fn check_pending_monomorphs<'ast>(&mut self, decls: &'ast [Decl<'ast>]) {
        let generic_decls: Vec<_> = decls
            .iter()
            .filter_map(|decl| match decl {
                Decl::GenericFunc(func_decl) => Some(func_decl),
                _ => None,
            })
            .collect();

        let mut cursor = 0;
        while cursor < self.monomorphs.pending().len() {
            let request = self.monomorphs.pending()[cursor].clone();
            cursor += 1;

            if self.monomorph_instances.iter().any(|instance| {
                instance.generic_name == request.generic_name
                    && instance.type_args == request.type_args
            }) {
                continue;
            }

            let Some(result) = self
                .monomorphs
                .get(request.generic_name, &request.type_args)
                .cloned()
            else {
                continue;
            };
            let Some(decl) = generic_decls
                .iter()
                .copied()
                .find(|decl| decl.name == request.generic_name)
            else {
                continue;
            };
            self.check_generic_instantiation(decl, &request.type_args, result);
        }
    }

    fn check_generic_instantiation<'ast>(
        &mut self,
        decl: &'ast GenericFuncDecl<'ast>,
        type_args: &[TypeId],
        result: MonomorphResult,
    ) {
        let signature = match self.types.kind(result.type_id).clone() {
            TypeKind::Function(signature) => signature,
            _ => return,
        };

        let saved_table = mem::take(&mut self.table);
        let saved_scopes = self.scopes.clone();
        let saved_infer = self.infer.clone();
        let saved_return_types = self.current_return_types.clone();
        let saved_impl_targets = self.current_impl_targets.clone();
        let baseline_errors = self.errors.len();

        self.table = TypeTable::new();
        self.scopes = saved_scopes.clone();
        self.infer.clear();
        self.current_return_types.clear();
        self.current_impl_targets.clear();

        self.table.insert(decl.id, result.type_id);
        if let Some(return_type) = decl.return_type {
            self.table.insert(return_type.id(), signature.return_type);
        }

        self.scopes.push_scope();
        for (param, ty) in decl.params.iter().zip(signature.params.iter()) {
            self.scopes.insert(param.name, *ty);
            self.table.insert(param.id, *ty);
            self.table.insert(param.ty.id(), *ty);
        }
        self.current_return_types.push(signature.return_type);
        self.visit_block(decl.body);
        self.current_return_types.pop();
        self.scopes.pop_scope();
        self.finalize_table_inferences();

        let instance_table = self.table.clone();
        self.table = saved_table;
        self.scopes = saved_scopes;
        self.infer = saved_infer;
        self.current_return_types = saved_return_types;
        self.current_impl_targets = saved_impl_targets;

        if self.errors.len() == baseline_errors {
            self.monomorph_instances.push(MonomorphInstance {
                generic_name: decl.name,
                type_args: type_args.to_vec(),
                result,
                table: instance_table,
            });
        }
    }

    fn current_return_type(&self) -> TypeId {
        self.current_return_types
            .last()
            .copied()
            .unwrap_or_else(|| self.types.unknown())
    }

    fn check_expr<'ast>(&mut self, expr: ExprRef<'ast>) -> TypeId {
        self.visit_expr(expr);
        self.table
            .get(expr.id())
            .unwrap_or_else(|| self.types.unknown())
    }

    fn unify(&mut self, expected: TypeId, found: TypeId, span: thagore_ast::Span) -> TypeId {
        self.infer
            .add_equality(expected, found, span, &self.types, &mut self.errors)
    }

    fn can_widen_argument(&mut self, expected: TypeId, found: TypeId) -> bool {
        let expected = self.resolved_type(expected);
        let found = self.resolved_type(found);
        expected == self.types.i64() && found == self.types.i32()
    }

    fn is_print_builtin_name(&self, name: thagore_ast::InternedStr) -> bool {
        self.symbol_name(name)
            .and_then(print_builtin_surface_name)
            .is_some()
    }

    fn is_printable_type(&mut self, ty: TypeId) -> bool {
        let resolved = self.resolved_type(ty);
        matches!(
            self.types.kind(resolved),
            TypeKind::Str | TypeKind::I32 | TypeKind::I64 | TypeKind::F64 | TypeKind::Bool
        ) || self.types.is_unknown(resolved)
            || self.types.is_infer(resolved)
    }

    fn visit_print_builtin_call(&mut self, expr: &CallExpr<'_>) {
        let callee = self.check_expr(expr.callee);
        if expr.args.len() != 1 {
            self.errors.push(TypeError::ArgumentCountMismatch {
                expected: 1,
                found: expr.args.len(),
                span: expr.span,
            });
            self.table.insert(expr.id, self.types.unknown());
            return;
        }

        let arg = self.check_expr(expr.args[0]);
        if !self.is_printable_type(arg) {
            let found = self.resolved_type(arg);
            self.errors.push(TypeError::TypeMismatch {
                expected: self.types.str(),
                found,
                span: expr.args[0].span(),
            });
            self.table.insert(expr.id, self.types.unknown());
            return;
        }

        let _ = callee;
        self.table.insert(expr.id, self.types.unit());
    }

    fn resolved_type(&mut self, ty: TypeId) -> TypeId {
        self.infer.sync_with_arena(&self.types);
        self.infer.resolve(ty)
    }

    fn require_bool(&mut self, ty: TypeId, span: thagore_ast::Span) {
        let resolved = self.resolved_type(ty);
        let bool_ty = self.types.bool();
        if resolved != bool_ty && !self.types.is_unknown(resolved) {
            self.errors.push(TypeError::ConditionNotBool {
                found: resolved,
                span,
            });
        } else {
            self.unify(bool_ty, ty, span);
        }
    }

    fn require_numeric(&mut self, ty: TypeId, span: thagore_ast::Span) -> TypeId {
        let resolved = self.resolved_type(ty);
        if self.types.is_numeric(resolved)
            || self.types.is_unknown(resolved)
            || self.types.is_infer(resolved)
        {
            return ty;
        }

        self.errors.push(TypeError::TypeMismatch {
            expected: self.types.i32(),
            found: resolved,
            span,
        });
        self.types.unknown()
    }

    fn bind_let_result(
        &mut self,
        name: thagore_ast::InternedStr,
        decl_id: thagore_ast::NodeId,
        span: thagore_ast::Span,
        declared: Option<TypeId>,
        inferred: TypeId,
    ) {
        let needs_inference = declared
            .map(|expected| self.types.is_infer(expected))
            .unwrap_or(false)
            || self.types.is_infer(inferred);
        let result = match declared {
            Some(expected) => {
                let unified = self.unify(expected, inferred, span);
                let resolved = self.resolved_type(unified);
                if needs_inference
                    && (self.types.is_infer(resolved) || self.types.is_unknown(resolved))
                {
                    self.errors.push(TypeError::InferenceFailure { span });
                    self.types.unknown()
                } else {
                    resolved
                }
            }
            None => self.default_inferred_type(inferred),
        };

        self.table.insert(decl_id, result);
        self.scopes.insert(name, result);
    }

    fn lookup_identifier(
        &mut self,
        name: thagore_ast::InternedStr,
        span: thagore_ast::Span,
    ) -> TypeId {
        self.scopes.get(&name).copied().unwrap_or_else(|| {
            self.errors
                .push(TypeError::UnknownIdentifier { name, span });
            self.types.unknown()
        })
    }

    fn method_for_field(
        &mut self,
        struct_name: thagore_ast::InternedStr,
        field: thagore_ast::InternedStr,
    ) -> Option<TypeId> {
        let full_method = self
            .method_types
            .get(&MethodKey {
                struct_name,
                method_name: field,
            })
            .copied()?;
        let TypeKind::Function(signature) = self.types.kind(full_method).clone() else {
            return None;
        };
        let params = if signature.params.is_empty() {
            Vec::new()
        } else {
            signature.params[1..].to_vec()
        };
        Some(self.types.intern_function(params, signature.return_type))
    }

    fn default_inferred_type(&mut self, inferred: TypeId) -> TypeId {
        let resolved = self.resolved_type(inferred);
        if self.types.is_infer(resolved) {
            let default = self.types.i32();
            self.unify(default, inferred, thagore_ast::Span::empty());
            default
        } else {
            resolved
        }
    }

    fn finalize_table_inferences(&mut self) {
        self.infer.sync_with_arena(&self.types);
        let default_i32 = self.types.i32();
        let infer = &mut self.infer;
        let types = &self.types;
        let table = &mut self.table;
        table.rewrite_all(|type_id| {
            let resolved = infer.resolve(type_id);
            if types.is_infer(resolved) {
                default_i32
            } else {
                resolved
            }
        });
    }

    fn finalize_inferred_function_signatures<'ast>(&mut self, decls: &'ast [Decl<'ast>]) {
        for decl in decls {
            match decl {
                Decl::Func(func_decl) => self.finalize_inferred_function(func_decl, None),
                Decl::Impl(impl_decl) => {
                    for method in impl_decl.methods {
                        self.finalize_inferred_function(
                            method,
                            Some(MethodKey {
                                struct_name: impl_decl.target,
                                method_name: method.name,
                            }),
                        );
                    }
                }
                _ => {}
            }
        }
    }

    fn finalize_inferred_function<'ast>(
        &mut self,
        decl: &'ast FuncDecl<'ast>,
        method_key: Option<MethodKey>,
    ) {
        if decl.return_type.is_some() {
            return;
        }

        let Some(function_type) = self.table.get(decl.id) else {
            return;
        };
        let TypeKind::Function(signature) = self.types.kind(function_type).clone() else {
            return;
        };
        let mut returns = Vec::new();
        collect_return_sites(decl.body, &mut returns);
        let resolved_returns = returns
            .into_iter()
            .map(|site| {
                let ty = match site.value {
                    Some(expr) => self
                        .table
                        .get(expr.id())
                        .map(|ty| self.default_inferred_type(ty))
                        .unwrap_or_else(|| self.types.unit()),
                    None => self.types.unit(),
                };
                ResolvedReturnType {
                    ty: self.resolved_type(ty),
                    span: site.span,
                }
            })
            .collect::<Vec<_>>();
        let inferred = match infer_function_return_type(
            &resolved_returns,
            block_guarantees_return(decl.body),
            self.types.unit(),
        ) {
            Ok(inferred) => inferred,
            Err(error) => {
                self.errors.push(TypeError::ReturnTypeMismatch {
                    expected: error.expected,
                    found: error.found,
                    span: error.span,
                });
                self.types.unknown()
            }
        };

        let unified = self.unify(signature.return_type, inferred, decl.span);
        let resolved_return = self.resolved_type(unified);
        let final_return = if self.types.is_unknown(resolved_return) {
            let expected = self.resolved_type(signature.return_type);
            self.errors.push(TypeError::ReturnTypeMismatch {
                expected,
                found: inferred,
                span: decl.span,
            });
            inferred
        } else {
            resolved_return
        };

        let final_signature = self
            .types
            .intern_function(signature.params.clone(), final_return);
        self.table.insert(decl.id, final_signature);
        self.scopes.insert(decl.name, final_signature);
        if let Some(method_key) = method_key {
            self.method_types.insert(method_key, final_signature);
        }
    }
}

fn print_builtin_surface_name(name: &str) -> Option<&'static str> {
    match name {
        "print" | "std__io__print" => Some("print"),
        "println" | "std__io__println" => Some("println"),
        "eprint" | "std__io__eprint" => Some("eprint"),
        "eprintln" | "std__io__eprintln" => Some("eprintln"),
        _ => None,
    }
}

impl<'ast> Visitor<'ast> for TypeChecker {
    fn visit_decl(&mut self, decl: DeclRef<'ast>) {
        match decl {
            Decl::Func(node) => self.visit_func_decl(node),
            Decl::GenericFunc(node) => self.visit_generic_func_decl(node),
            Decl::Let(node) => self.visit_let_decl(node),
            Decl::Const(node) => self.visit_const_decl(node),
            Decl::Struct(node) => self.visit_struct_decl(node),
            Decl::GenericStruct(node) => {
                self.table.insert(node.id, self.types.unit());
            }
            Decl::Impl(node) => self.visit_impl_block(node),
            Decl::GenericImpl(node) => {
                self.table.insert(node.id, self.types.unit());
            }
            Decl::Import(node) => self.visit_import_decl(node),
            Decl::Extern(node) => self.visit_extern_decl(node),
            Decl::Intent(node) => self.visit_intent_decl(node),
            Decl::Flow(node) => self.visit_flow_decl(node),
        }
    }

    fn visit_func_decl(&mut self, decl: &'ast FuncDecl<'ast>) {
        let function_type = self
            .table
            .get(decl.id)
            .unwrap_or_else(|| self.collect_function_signature(decl, None));
        self.scopes.push_scope();

        if let TypeKind::Function(signature) = self.types.kind(function_type).clone() {
            for (param, ty) in decl.params.iter().zip(signature.params) {
                self.table.insert(param.id, ty);
                self.scopes.insert(param.name, ty);
            }
            self.current_return_types.push(signature.return_type);
        } else {
            self.current_return_types.push(self.types.unknown());
        }

        self.visit_block(decl.body);
        self.current_return_types.pop();
        self.scopes.pop_scope();
    }

    fn visit_let_decl(&mut self, decl: &'ast LetDecl<'ast>) {
        let declared = decl.ty.map(|ty| self.resolve_type_expr(ty));
        let inferred = self.check_expr(decl.initializer);
        self.bind_let_result(decl.name, decl.id, decl.span, declared, inferred);
    }

    fn visit_generic_func_decl(&mut self, decl: &'ast GenericFuncDecl<'ast>) {
        let template = self.extract_generic_function_template(decl);
        self.generic_functions.insert(decl.name, template);
        self.table.insert(decl.id, self.types.unit());
    }

    fn visit_const_decl(&mut self, decl: &'ast ConstDecl<'ast>) {
        let declared = self.resolve_type_expr(decl.type_ann);
        let inferred = self.check_expr(decl.value);
        let result = self.unify(declared, inferred, decl.span);
        let resolved = self.resolved_type(result);
        self.table.insert(decl.id, resolved);
        self.scopes.insert(decl.name, resolved);
    }

    fn visit_struct_decl(&mut self, decl: &'ast StructDecl<'ast>) {
        let struct_id = self
            .struct_types
            .get(&decl.name)
            .copied()
            .unwrap_or_else(|| self.types.reserve_struct(decl.name));
        self.table.insert(decl.id, struct_id);
        for field in decl.fields {
            let field_ty = self.resolve_type_expr(field.ty);
            self.table.insert(field.id, field_ty);
        }
    }

    fn visit_impl_block(&mut self, decl: &'ast thagore_ast::ImplBlock<'ast>) {
        let target = self
            .struct_types
            .get(&decl.target)
            .copied()
            .unwrap_or_else(|| self.types.unknown());
        self.current_impl_targets.push(target);
        for method in decl.methods {
            let method_type = self
                .method_types
                .get(&MethodKey {
                    struct_name: decl.target,
                    method_name: method.name,
                })
                .copied()
                .unwrap_or_else(|| self.collect_function_signature(method, Some(target)));
            self.table.insert(method.id, method_type);
            self.visit_func_decl(method);
        }
        self.current_impl_targets.pop();
    }

    fn visit_import_decl(&mut self, decl: &'ast ImportDecl<'ast>) {
        self.table.insert(decl.id, self.types.unit());
    }

    fn visit_extern_decl(&mut self, decl: &'ast ExternDecl<'ast>) {
        let signature = self
            .scopes
            .get(&decl.name)
            .copied()
            .unwrap_or_else(|| self.collect_extern_signature(decl));
        self.table.insert(decl.id, signature);
    }

    fn visit_intent_decl(&mut self, decl: &'ast IntentDecl<'ast>) {
        self.scopes.push_scope();
        self.current_return_types.push(self.types.unknown());
        for constraint in decl.constraints {
            self.check_expr(*constraint);
        }
        self.visit_block(decl.body);
        self.current_return_types.pop();
        self.scopes.pop_scope();
        self.table.insert(decl.id, self.types.unit());
    }

    fn visit_flow_decl(&mut self, decl: &'ast FlowDecl<'ast>) {
        self.scopes.push_scope();
        self.current_return_types.push(self.types.unknown());
        for stage in decl.stages {
            self.visit_flow_stage(stage);
        }
        if let Some(compensation) = decl.compensation {
            self.visit_block(compensation);
        }
        self.current_return_types.pop();
        self.scopes.pop_scope();
        self.table.insert(decl.id, self.types.unit());
    }

    fn visit_flow_stage(&mut self, stage: &'ast FlowStage<'ast>) {
        self.visit_block(stage.body);
        self.table.insert(stage.id, self.types.unit());
    }

    fn visit_stmt(&mut self, stmt: StmtRef<'ast>) {
        match stmt {
            Stmt::Let(node) => self.visit_let_decl(node),
            Stmt::Expr(node) => self.visit_expr_stmt(node),
            Stmt::Return(node) => self.visit_return_stmt(node),
            Stmt::If(node) => self.visit_if_stmt(node),
            Stmt::While(node) => self.visit_while_stmt(node),
            Stmt::For(node) => self.visit_for_stmt(node),
            Stmt::Break(node) => self.visit_break_stmt(node),
            Stmt::Continue(node) => self.visit_continue_stmt(node),
        }
    }

    fn visit_block(&mut self, block: BlockRef<'ast>) {
        self.scopes.push_scope();
        for stmt in block.statements {
            self.visit_stmt(stmt);
        }
        self.scopes.pop_scope();
        self.table.insert(block.id, self.types.unit());
    }

    fn visit_expr_stmt(&mut self, stmt: &'ast ExprStmt<'ast>) {
        let ty = self.check_expr(stmt.expr);
        self.table.insert(stmt.id, ty);
    }

    fn visit_return_stmt(&mut self, stmt: &'ast ReturnStmt<'ast>) {
        let expected = self.current_return_type();
        let found = stmt
            .value
            .map(|expr| self.check_expr(expr))
            .unwrap_or_else(|| self.types.unit());

        let resolved_expected = self.resolved_type(expected);
        let resolved_found = self.resolved_type(found);
        if !self.types.is_unknown(resolved_expected) && !self.types.is_unknown(resolved_found) {
            let unified = self.unify(expected, found, stmt.span);
            let unified_resolved = self.resolved_type(unified);
            if self.types.is_unknown(unified_resolved) {
                self.errors.push(TypeError::ReturnTypeMismatch {
                    expected: resolved_expected,
                    found: resolved_found,
                    span: stmt.span,
                });
            }
        }
        self.table.insert(stmt.id, expected);
    }

    fn visit_if_stmt(&mut self, stmt: &'ast IfStmt<'ast>) {
        let condition = self.check_expr(stmt.condition);
        self.require_bool(condition, stmt.condition.span());
        self.visit_block(stmt.then_block);
        if let Some(else_block) = stmt.else_block {
            self.visit_block(else_block);
        }
        self.table.insert(stmt.id, self.types.unit());
    }

    fn visit_while_stmt(&mut self, stmt: &'ast WhileStmt<'ast>) {
        let condition = self.check_expr(stmt.condition);
        self.require_bool(condition, stmt.condition.span());
        self.visit_block(stmt.body);
        self.table.insert(stmt.id, self.types.unit());
    }

    fn visit_for_stmt(&mut self, stmt: &'ast ForStmt<'ast>) {
        let iterator = self.check_expr(stmt.iterator);
        let resolved_iterator = self.resolved_type(iterator);
        let element_type = match self.types.kind(resolved_iterator) {
            TypeKind::Array(element) => *element,
            TypeKind::Unknown => self.types.unknown(),
            _ => {
                self.errors.push(TypeError::NotIndexable {
                    found: resolved_iterator,
                    span: stmt.iterator.span(),
                });
                self.types.unknown()
            }
        };

        self.scopes.push_scope();
        self.scopes.insert(stmt.binding, element_type);
        self.visit_block(stmt.body);
        self.scopes.pop_scope();
        self.table.insert(stmt.id, self.types.unit());
    }

    fn visit_break_stmt(&mut self, stmt: &'ast thagore_ast::BreakStmt) {
        self.table.insert(stmt.id, self.types.unit());
    }

    fn visit_continue_stmt(&mut self, stmt: &'ast thagore_ast::ContinueStmt) {
        self.table.insert(stmt.id, self.types.unit());
    }

    fn visit_expr(&mut self, expr: ExprRef<'ast>) {
        match expr {
            Expr::Binary(node) => self.visit_binary_expr(node),
            Expr::Unary(node) => self.visit_unary_expr(node),
            Expr::Call(node) => self.visit_call_expr(node),
            Expr::FieldAccess(node) => self.visit_field_access_expr(node),
            Expr::Index(node) => self.visit_index_expr(node),
            Expr::Ident(node) => self.visit_ident_expr(node),
            Expr::Literal(node) => self.visit_lit_expr(node),
            Expr::Assign(node) => self.visit_assign_expr(node),
        }
    }

    fn visit_binary_expr(&mut self, expr: &'ast BinaryExpr<'ast>) {
        let left = self.check_expr(expr.left);
        let right = self.check_expr(expr.right);
        let result = match expr.op {
            BinOp::Add | BinOp::Sub | BinOp::Mul | BinOp::Div | BinOp::Rem => {
                self.require_numeric(left, expr.left.span());
                self.require_numeric(right, expr.right.span());
                self.unify(left, right, expr.span)
            }
            BinOp::Eq | BinOp::NotEq | BinOp::Lt | BinOp::LtEq | BinOp::Gt | BinOp::GtEq => {
                self.unify(left, right, expr.span);
                self.types.bool()
            }
            BinOp::And | BinOp::Or => {
                self.require_bool(left, expr.left.span());
                self.require_bool(right, expr.right.span());
                self.types.bool()
            }
        };
        let resolved = self.resolved_type(result);
        self.table.insert(expr.id, resolved);
    }

    fn visit_unary_expr(&mut self, expr: &'ast UnaryExpr<'ast>) {
        let operand = self.check_expr(expr.operand);
        let result = match expr.op {
            UnaryOp::Not => {
                self.require_bool(operand, expr.operand.span());
                self.types.bool()
            }
            UnaryOp::Neg | UnaryOp::Plus => {
                self.require_numeric(operand, expr.operand.span());
                operand
            }
        };
        let resolved = self.resolved_type(result);
        self.table.insert(expr.id, resolved);
    }

    fn visit_call_expr(&mut self, expr: &'ast CallExpr<'ast>) {
        if let Expr::Ident(ident) = expr.callee {
            if let Some(template) = self.generic_functions.get(&ident.name).cloned() {
                if let Some((function_type, return_type)) =
                    self.instantiate_generic_function(&template, expr.args, expr.span)
                {
                    self.table.insert(ident.id, function_type);
                    self.table.insert(expr.id, return_type);
                } else {
                    self.table.insert(expr.id, self.types.unknown());
                }
                return;
            }
        }

        let is_print_call = match expr.callee {
            Expr::Ident(ident) => self.is_print_builtin_name(ident.name),
            Expr::FieldAccess(field) => self
                .symbol_name(field.field)
                .and_then(print_builtin_surface_name)
                .is_some(),
            _ => false,
        };

        if is_print_call {
            self.visit_print_builtin_call(expr);
            return;
        }

        let callee = self.check_expr(expr.callee);
        let callee_resolved = self.resolved_type(callee);
        let TypeKind::Function(signature) = self.types.kind(callee_resolved).clone() else {
            self.errors.push(TypeError::NotCallable {
                found: callee_resolved,
                span: expr.callee.span(),
            });
            self.table.insert(expr.id, self.types.unknown());
            return;
        };

        if signature.params.len() != expr.args.len() {
            self.errors.push(TypeError::ArgumentCountMismatch {
                expected: signature.params.len(),
                found: expr.args.len(),
                span: expr.span,
            });
        }

        for (expected, arg) in signature.params.iter().zip(expr.args.iter()) {
            let found = self.check_expr(arg);
            if self.can_widen_argument(*expected, found) {
                continue;
            }
            self.unify(*expected, found, arg.span());
        }

        self.table.insert(expr.id, signature.return_type);
    }

    fn visit_field_access_expr(&mut self, expr: &'ast FieldAccessExpr<'ast>) {
        let object = self.check_expr(expr.object);
        let object_resolved = self.resolved_type(object);
        let result = match self.types.kind(object_resolved).clone() {
            TypeKind::Struct(struct_ty) => {
                if let Some(field) = struct_ty
                    .fields
                    .iter()
                    .find(|field| field.name == expr.field)
                {
                    field.ty
                } else if let Some(method) = self.method_for_field(struct_ty.name, expr.field) {
                    method
                } else {
                    self.errors.push(TypeError::UnknownField {
                        struct_name: struct_ty.name,
                        field: expr.field,
                        span: expr.span,
                    });
                    self.types.unknown()
                }
            }
            TypeKind::Unknown => self.types.unknown(),
            _ => {
                self.errors.push(TypeError::Unknown {
                    span: expr.object.span(),
                });
                self.types.unknown()
            }
        };
        self.table.insert(expr.id, result);
    }

    fn visit_index_expr(&mut self, expr: &'ast IndexExpr<'ast>) {
        let object = self.check_expr(expr.object);
        let index = self.check_expr(expr.index);
        self.unify(self.types.i32(), index, expr.index.span());
        let object_resolved = self.resolved_type(object);
        let result = match self.types.kind(object_resolved) {
            TypeKind::Array(element) => *element,
            TypeKind::Unknown => self.types.unknown(),
            _ => {
                self.errors.push(TypeError::NotIndexable {
                    found: object_resolved,
                    span: expr.object.span(),
                });
                self.types.unknown()
            }
        };
        self.table.insert(expr.id, result);
    }

    fn visit_ident_expr(&mut self, expr: &'ast IdentExpr) {
        let ty = self.lookup_identifier(expr.name, expr.span);
        self.table.insert(expr.id, ty);
    }

    fn visit_lit_expr(&mut self, expr: &'ast LitExpr) {
        let ty = match expr.literal {
            Literal::Int(_) => {
                let infer = self.types.fresh_int_infer();
                self.infer.sync_with_arena(&self.types);
                infer
            }
            Literal::Float(_) => self.types.f64(),
            Literal::Bool(_) => self.types.bool(),
            Literal::Str(_) => self.types.str(),
        };
        self.table.insert(expr.id, ty);
    }

    fn visit_assign_expr(&mut self, expr: &'ast AssignExpr<'ast>) {
        let target = self.check_expr(expr.target);
        let value = self.check_expr(expr.value);
        let result = self.unify(target, value, expr.span);
        let resolved = self.resolved_type(result);
        self.table.insert(expr.id, resolved);
    }

    fn visit_param(&mut self, param: &'ast Param<'ast>) {
        let ty = self.resolve_type_expr(param.ty);
        self.table.insert(param.id, ty);
    }

    fn visit_field_def(&mut self, field: &'ast FieldDef<'ast>) {
        let ty = self.resolve_type_expr(field.ty);
        self.table.insert(field.id, ty);
    }

    fn visit_type_expr(&mut self, ty: TypeExprRef<'ast>) {
        let resolved = self.resolve_type_expr(ty);
        self.table.insert(ty.id(), resolved);
    }

    fn visit_named_type_expr(&mut self, ty: &'ast NamedTypeExpr) {
        let resolved = self.resolve_named_type(ty);
        self.table.insert(ty.id, resolved);
    }

    fn visit_generic_type_expr(&mut self, ty: &'ast GenericTypeExpr<'ast>) {
        let resolved = self.resolve_generic_type(ty);
        self.table.insert(ty.id, resolved);
    }

    fn visit_infer_type_expr(&mut self, ty: &'ast InferTypeExpr) {
        let infer = self.types.fresh_infer();
        self.infer.sync_with_arena(&self.types);
        self.table.insert(ty.id, infer);
    }
}
