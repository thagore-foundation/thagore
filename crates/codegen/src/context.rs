//! Shared LLVM code generation context and per-function state.

use std::collections::HashMap;

use inkwell::basic_block::BasicBlock as LlvmBasicBlock;
use inkwell::builder::Builder;
use inkwell::context::Context;
use inkwell::module::Module;
use inkwell::types::BasicTypeEnum;
use inkwell::values::{BasicValueEnum, FunctionValue};
use thagore_ast::{InternedStr, Span};
use thagore_ir::{BlockId, Value};
use thagore_typeck::{TypeArena, TypeId};

use crate::error::CodegenError;
use crate::types::TypeMap;

/// Mapping from IR SSA values to LLVM values.
pub type ValueMap<'ctx> = HashMap<Value, BasicValueEnum<'ctx>>;

/// Mapping from IR blocks to LLVM basic blocks.
pub type BlockMap<'ctx> = HashMap<BlockId, LlvmBasicBlock<'ctx>>;

/// Shared backend state for a single LLVM module emission.
pub struct CodegenContext<'ctx> {
    /// LLVM context.
    pub llvm: &'ctx Context,
    /// LLVM module receiving all emitted IR.
    pub module: Module<'ctx>,
    /// Shared instruction builder.
    pub builder: Builder<'ctx>,
    /// Precomputed Thagore-to-LLVM type mapping.
    pub type_map: TypeMap<'ctx>,
    /// Canonical type arena from the type checker.
    pub types: TypeArena,
    /// Printable symbol names used for functions and structs.
    pub symbol_names: HashMap<InternedStr, String>,
    /// Declared LLVM functions by Thagore symbol.
    pub functions: HashMap<InternedStr, FunctionValue<'ctx>>,
    /// Cached trap intrinsic declaration.
    pub trap_intrinsic: FunctionValue<'ctx>,
}

impl<'ctx> CodegenContext<'ctx> {
    /// Creates a fresh LLVM module context.
    pub fn new(
        llvm: &'ctx Context,
        module_name: &str,
        types: &TypeArena,
        symbol_names: HashMap<InternedStr, String>,
    ) -> Result<Self, Vec<CodegenError>> {
        let module = llvm.create_module(module_name);
        let builder = llvm.create_builder();
        let type_map = TypeMap::new(llvm, types, |symbol| {
            symbol_names
                .get(&symbol)
                .cloned()
                .unwrap_or_else(|| format!("sym_{}", symbol.as_u32()))
        })?;
        let trap_intrinsic = declare_trap(llvm, &module);

        Ok(Self {
            llvm,
            module,
            builder,
            type_map,
            types: types.clone(),
            symbol_names,
            functions: HashMap::new(),
            trap_intrinsic,
        })
    }

    /// Returns a stable printable symbol name.
    #[must_use]
    pub fn symbol_name(&self, symbol: InternedStr) -> String {
        self.symbol_names
            .get(&symbol)
            .cloned()
            .unwrap_or_else(|| format!("sym_{}", symbol.as_u32()))
    }

    /// Returns the runtime LLVM type for a `TypeId`.
    pub fn basic_type(&self, ty: TypeId) -> Result<BasicTypeEnum<'ctx>, CodegenError> {
        self.type_map
            .basic_type(ty)
            .ok_or(CodegenError::MissingType { ty, span: None })
    }

    /// Emits a call to `llvm.trap()` and an `unreachable` terminator.
    pub fn emit_trap(&self, span: Option<Span>) -> CodegenError {
        let _ = self.builder.build_call(self.trap_intrinsic, &[], "trap");
        let _ = self.builder.build_unreachable();
        CodegenError::unknown(span)
    }
}

/// Per-function backend state.
pub struct FunctionContext<'ctx> {
    /// Lowered LLVM function.
    pub function: FunctionValue<'ctx>,
    /// Value lookup table.
    pub values: ValueMap<'ctx>,
    /// Per-value type side table copied from IR.
    pub value_types: Vec<TypeId>,
    /// Block lookup table.
    pub blocks: BlockMap<'ctx>,
    /// Printable function symbol.
    pub name: InternedStr,
}

impl<'ctx> FunctionContext<'ctx> {
    /// Creates a fresh per-function codegen state.
    #[must_use]
    pub fn new(function: FunctionValue<'ctx>, name: InternedStr, value_types: Vec<TypeId>) -> Self {
        Self {
            function,
            values: HashMap::new(),
            value_types,
            blocks: HashMap::new(),
            name,
        }
    }
}

fn declare_trap<'ctx>(llvm: &'ctx Context, module: &Module<'ctx>) -> FunctionValue<'ctx> {
    if let Some(existing) = module.get_function("llvm.trap") {
        return existing;
    }
    let void_type = llvm.void_type();
    module.add_function("llvm.trap", void_type.fn_type(&[], false), None)
}
