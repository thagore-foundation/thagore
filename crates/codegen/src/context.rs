//! Shared LLVM code generation context and per-function state.

use std::collections::HashMap;

use inkwell::basic_block::BasicBlock as LlvmBasicBlock;
use inkwell::builder::Builder;
use inkwell::context::Context;
use inkwell::module::Module;
use inkwell::targets::{
    CodeModel, InitializationConfig, RelocMode, Target, TargetMachine, TargetTriple,
};
use inkwell::types::BasicTypeEnum;
use inkwell::values::InstructionValue;
use inkwell::values::{BasicValueEnum, FunctionValue};
use thagore_ast::{InternedStr, Span};
use thagore_ir::{BlockId, Value};
use thagore_typeck::{TypeArena, TypeId};

use crate::error::CodegenError;
use crate::optimize::OptimizationLevel;
use crate::types::TypeMap;

/// Mapping from IR SSA values to LLVM values.
pub type ValueMap<'ctx> = HashMap<Value, BasicValueEnum<'ctx>>;

/// Mapping from IR blocks to LLVM basic blocks.
pub type BlockMap<'ctx> = HashMap<BlockId, LlvmBasicBlock<'ctx>>;

/// Target machine configuration shared by LLVM IR emission and object output.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TargetMachineConfig {
    /// Explicit target triple. Defaults to the host triple when omitted.
    pub triple: Option<String>,
    /// Target CPU name used for code generation.
    pub cpu: String,
    /// Target feature string passed to LLVM.
    pub features: String,
    /// Relocation model for emitted objects.
    pub reloc_mode: RelocMode,
    /// LLVM code model.
    pub code_model: CodeModel,
    /// Enables fast floating-point assumptions in emitted function attributes.
    pub fast_math: bool,
}

impl Default for TargetMachineConfig {
    fn default() -> Self {
        Self {
            triple: None,
            cpu: host_cpu_name(),
            features: host_cpu_features(),
            reloc_mode: RelocMode::PIC,
            code_model: CodeModel::Default,
            fast_math: true,
        }
    }
}

impl TargetMachineConfig {
    /// Returns the resolved LLVM target triple.
    #[must_use]
    pub fn triple(&self) -> TargetTriple {
        self.triple
            .as_deref()
            .map(TargetTriple::create)
            .unwrap_or_else(TargetMachine::get_default_triple)
    }
}

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
    /// Active target machine configuration.
    pub target: TargetMachineConfig,
    /// Declared LLVM functions by Thagore symbol.
    pub functions: HashMap<InternedStr, FunctionValue<'ctx>>,
    /// Cached trap intrinsic declaration.
    pub trap_intrinsic: FunctionValue<'ctx>,
    /// Metadata kind id for `!llvm.loop`.
    pub loop_metadata_kind: u32,
}

impl<'ctx> CodegenContext<'ctx> {
    /// Creates a fresh LLVM module context.
    pub fn new(
        llvm: &'ctx Context,
        module_name: &str,
        types: &TypeArena,
        optimization: OptimizationLevel,
        target: TargetMachineConfig,
        symbol_names: HashMap<InternedStr, String>,
    ) -> Result<Self, Vec<CodegenError>> {
        let module = llvm.create_module(module_name);
        let builder = llvm.create_builder();
        let target_machine = create_target_machine(&target, optimization)
            .map_err(|error| vec![error])?;
        module.set_triple(&target_machine.get_triple());
        module.set_data_layout(&target_machine.get_target_data().get_data_layout());
        let type_map = TypeMap::new(llvm, types, |symbol| {
            symbol_names
                .get(&symbol)
                .cloned()
                .unwrap_or_else(|| format!("sym_{}", symbol.as_u32()))
        })?;
        let trap_intrinsic = declare_trap(llvm, &module);
        let loop_metadata_kind = llvm.get_kind_id("llvm.loop");

        Ok(Self {
            llvm,
            module,
            builder,
            type_map,
            types: types.clone(),
            symbol_names,
            target,
            functions: HashMap::new(),
            trap_intrinsic,
            loop_metadata_kind,
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

    /// Attaches vectorization and unroll hints to a loop back-edge terminator.
    pub fn annotate_loop_branch(
        &self,
        instruction: InstructionValue<'ctx>,
    ) -> Result<(), CodegenError> {
        let vectorize = self.llvm.metadata_node(&[
            self.llvm
                .metadata_string("llvm.loop.vectorize.enable")
                .into(),
            self.llvm.bool_type().const_int(1, false).into(),
        ]);
        let unroll = self.llvm.metadata_node(&[
            self.llvm.metadata_string("llvm.loop.unroll.enable").into(),
            self.llvm.bool_type().const_int(1, false).into(),
        ]);
        let interleave = self.llvm.metadata_node(&[
            self.llvm
                .metadata_string("llvm.loop.interleave.count")
                .into(),
            self.llvm.i32_type().const_int(4, false).into(),
        ]);
        let loop_node = self.llvm.metadata_node(&[
            self.llvm.metadata_string("llvm.loop").into(),
            vectorize.into(),
            unroll.into(),
            interleave.into(),
        ]);
        instruction
            .set_metadata(loop_node, self.loop_metadata_kind)
            .map_err(|message| CodegenError::OutputFailed {
                artifact: "llvm.loop".into(),
                message: message.to_string(),
            })
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

/// Creates an LLVM target machine using the configured triple, native CPU
/// tuning, PIC relocation, and the requested optimization level.
pub fn create_target_machine(
    config: &TargetMachineConfig,
    opt_level: OptimizationLevel,
) -> Result<TargetMachine, CodegenError> {
    Target::initialize_all(&InitializationConfig::default());
    let triple = config.triple();
    let target = Target::from_triple(&triple).map_err(|error| CodegenError::OutputFailed {
        artifact: "target".into(),
        message: error.to_string(),
    })?;
    target
        .create_target_machine(
            &triple,
            &config.cpu,
            &config.features,
            opt_level.to_llvm(),
            config.reloc_mode,
            config.code_model,
        )
        .ok_or(CodegenError::OutputFailed {
            artifact: "target-machine".into(),
            message: format!(
                "failed to create target machine for triple {} cpu {} features {}",
                triple.as_str().to_string_lossy(),
                config.cpu,
                config.features
            ),
        })
}

fn host_cpu_name() -> String {
    let cpu = TargetMachine::get_host_cpu_name().to_string();
    if cpu.is_empty() {
        "generic".into()
    } else {
        cpu
    }
}

fn host_cpu_features() -> String {
    let features = TargetMachine::get_host_cpu_features().to_string();
    if features.is_empty() {
        String::new()
    } else {
        features
    }
}
