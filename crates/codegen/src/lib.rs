//! LLVM code generation for the Thagore compiler.

use std::collections::HashMap;

use inkwell::context::Context;
use thagore_ast::InternedStr;
use thagore_ir::IrModule;
use thagore_typeck::TypeArena;

pub mod context;
pub mod debug;
pub mod error;
pub mod func;
pub mod instr;
pub mod optimize;
pub mod output;
pub mod structs;
pub mod term;
pub mod types;

pub use crate::context::{BlockMap, CodegenContext, FunctionContext, ValueMap};
pub use crate::debug::{DebugOptions, DebugState};
pub use crate::error::CodegenError;
pub use crate::optimize::OptimizationLevel;
pub use crate::output::{
    emit_bitcode, emit_llvm_ir, emit_object, emit_outputs, link_binary, OutputArtifacts,
    OutputConfig,
};
pub use crate::types::TypeMap;

/// High-level code generation configuration.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct CodegenOptions {
    /// Optimization level for the emitted LLVM module.
    pub optimization: OptimizationLevel,
    /// Debug emission configuration.
    pub debug: DebugOptions,
    /// Optional output artifact destinations.
    pub output: OutputConfig,
}

/// Result of a completed code generation run.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct CodegenResult {
    /// Printable LLVM IR for the generated module.
    pub llvm_ir: String,
    /// Emitted output artifact paths.
    pub artifacts: OutputArtifacts,
}

/// High-level LLVM backend entry point.
#[derive(Debug, Default)]
pub struct Codegen {
    options: CodegenOptions,
    symbol_names: HashMap<InternedStr, String>,
}

impl Codegen {
    /// Creates a new code generator with default options.
    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Replaces the active code generation options.
    pub fn set_options(&mut self, options: CodegenOptions) {
        self.options = options;
    }

    /// Registers a printable name for an interned symbol.
    pub fn register_symbol_name(&mut self, symbol: InternedStr, name: &str) {
        self.symbol_names.insert(symbol, name.to_string());
    }

    /// Emits LLVM IR and optional output artifacts for a lowered IR module.
    pub fn emit(
        &self,
        ir_module: &IrModule,
        types: &TypeArena,
    ) -> Result<CodegenResult, Vec<CodegenError>> {
        let llvm = Context::create();
        let mut context = CodegenContext::new(
            &llvm,
            &self
                .symbol_names
                .get(&ir_module.name)
                .cloned()
                .unwrap_or_else(|| format!("module_{}", ir_module.name.as_u32())),
            types,
            self.symbol_names.clone(),
        )?;

        let mut errors = Vec::new();
        if let Err(mut struct_errors) = structs::declare_structs(&mut context, &ir_module.structs) {
            errors.append(&mut struct_errors);
        }
        if let Err(mut func_errors) = func::declare_functions(&mut context, ir_module) {
            errors.append(&mut func_errors);
        }
        if let Err(mut body_errors) = func::emit_functions(&mut context, ir_module) {
            errors.append(&mut body_errors);
        }
        if let Err(message) = context.module.verify() {
            errors.push(CodegenError::ModuleVerificationFailed {
                message: message.to_string(),
            });
        }

        optimize::optimize_module(&context.module, self.options.optimization);

        let llvm_ir = context.module.print_to_string().to_string();
        let artifacts = match emit_outputs(
            &context.module,
            &self.options.output,
            self.options.optimization,
        ) {
            Ok(artifacts) => artifacts,
            Err(mut output_errors) => {
                errors.append(&mut output_errors);
                OutputArtifacts::default()
            }
        };

        if errors.is_empty() {
            Ok(CodegenResult { llvm_ir, artifacts })
        } else {
            Err(errors)
        }
    }
}
