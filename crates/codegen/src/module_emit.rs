//! Per-module object emission helpers.

use std::path::{Path, PathBuf};

use crate::error::CodegenError;
use crate::{Codegen, CodegenOptions, OutputConfig};
use thagore_ir::IrModule;
use thagore_typeck::TypeArena;

/// Emits a single module to an object file.
pub fn emit_module(
    codegen: &mut Codegen,
    options: CodegenOptions,
    ir_module: &IrModule,
    types: &TypeArena,
    object_path: &Path,
    llvm_ir_path: Option<PathBuf>,
    bitcode_path: Option<PathBuf>,
) -> Result<PathBuf, Vec<CodegenError>> {
    let mut options = options;
    options.output = OutputConfig {
        llvm_ir: llvm_ir_path,
        bitcode: bitcode_path,
        object: Some(object_path.to_path_buf()),
        binary: None,
    };
    codegen.set_options(options);
    codegen.emit(ir_module, types)?;
    Ok(object_path.to_path_buf())
}
