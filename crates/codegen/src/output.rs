//! Output artifact emission for the Thagore LLVM backend.

use std::path::{Path, PathBuf};
use std::process::Command;

use inkwell::module::Module;
use inkwell::targets::{
    CodeModel, FileType, InitializationConfig, RelocMode, Target, TargetMachine,
};

use crate::error::CodegenError;
use crate::optimize::OptimizationLevel;

/// Files emitted by the backend.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct OutputArtifacts {
    /// Emitted LLVM IR text path.
    pub llvm_ir: Option<PathBuf>,
    /// Emitted bitcode path.
    pub bitcode: Option<PathBuf>,
    /// Emitted object file path.
    pub object: Option<PathBuf>,
    /// Emitted native binary path.
    pub binary: Option<PathBuf>,
}

/// Optional output destinations requested by the caller.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct OutputConfig {
    /// Destination for `.ll`.
    pub llvm_ir: Option<PathBuf>,
    /// Destination for `.bc`.
    pub bitcode: Option<PathBuf>,
    /// Destination for `.o`.
    pub object: Option<PathBuf>,
    /// Destination for native binary.
    pub binary: Option<PathBuf>,
}

/// Emits all requested artifacts for the LLVM module.
pub fn emit_outputs(
    module: &Module<'_>,
    output: &OutputConfig,
    opt_level: OptimizationLevel,
) -> Result<OutputArtifacts, Vec<CodegenError>> {
    let mut artifacts = OutputArtifacts::default();
    let mut errors = Vec::new();

    if let Some(path) = &output.llvm_ir {
        if let Err(error) = emit_llvm_ir(module, path) {
            errors.push(error);
        } else {
            artifacts.llvm_ir = Some(path.clone());
        }
    }

    if let Some(path) = &output.bitcode {
        if let Err(error) = emit_bitcode(module, path) {
            errors.push(error);
        } else {
            artifacts.bitcode = Some(path.clone());
        }
    }

    let target_machine = if output.object.is_some() || output.binary.is_some() {
        match create_target_machine(opt_level) {
            Ok(machine) => Some(machine),
            Err(error) => {
                errors.push(error);
                None
            }
        }
    } else {
        None
    };

    if let (Some(path), Some(machine)) = (&output.object, target_machine.as_ref()) {
        if let Err(error) = emit_object(machine, module, path) {
            errors.push(error);
        } else {
            artifacts.object = Some(path.clone());
        }
    }

    if let Some(binary) = &output.binary {
        let object_path = output
            .object
            .clone()
            .unwrap_or_else(|| binary.with_extension("o"));
        if artifacts.object.is_none() {
            if let Some(machine) = target_machine.as_ref() {
                if let Err(error) = emit_object(machine, module, &object_path) {
                    errors.push(error);
                } else {
                    artifacts.object = Some(object_path.clone());
                }
            }
        }
        if artifacts.object.is_some() {
            if let Err(error) = link_binary(&object_path, binary) {
                errors.push(error);
            } else {
                artifacts.binary = Some(binary.clone());
            }
        }
    }

    if errors.is_empty() {
        Ok(artifacts)
    } else {
        Err(errors)
    }
}

/// Writes LLVM IR text to disk.
pub fn emit_llvm_ir(module: &Module<'_>, path: &Path) -> Result<(), CodegenError> {
    module
        .print_to_file(path)
        .map_err(|error| CodegenError::OutputFailed {
            artifact: path.display().to_string(),
            message: error.to_string(),
        })
}

/// Writes LLVM bitcode to disk.
pub fn emit_bitcode(module: &Module<'_>, path: &Path) -> Result<(), CodegenError> {
    if module.write_bitcode_to_path(path) {
        Ok(())
    } else {
        Err(CodegenError::OutputFailed {
            artifact: path.display().to_string(),
            message: "failed to write bitcode".into(),
        })
    }
}

/// Emits an object file using the system LLVM target.
pub fn emit_object(
    target_machine: &TargetMachine,
    module: &Module<'_>,
    path: &Path,
) -> Result<(), CodegenError> {
    target_machine
        .write_to_file(module, FileType::Object, path)
        .map_err(|error| CodegenError::OutputFailed {
            artifact: path.display().to_string(),
            message: error.to_string(),
        })
}

/// Links an object file into a native executable via `cc`.
pub fn link_binary(object: &Path, binary: &Path) -> Result<(), CodegenError> {
    let output = Command::new("cc")
        .arg(object)
        .arg("-o")
        .arg(binary)
        .output()
        .map_err(|error| CodegenError::LinkFailed {
            linker: "cc".into(),
            message: error.to_string(),
        })?;
    if output.status.success() {
        Ok(())
    } else {
        Err(CodegenError::LinkFailed {
            linker: "cc".into(),
            message: String::from_utf8_lossy(&output.stderr).into_owned(),
        })
    }
}

fn create_target_machine(opt_level: OptimizationLevel) -> Result<TargetMachine, CodegenError> {
    Target::initialize_all(&InitializationConfig::default());
    let triple = TargetMachine::get_default_triple();
    let target = Target::from_triple(&triple).map_err(|error| CodegenError::OutputFailed {
        artifact: "target".into(),
        message: error.to_string(),
    })?;
    target
        .create_target_machine(
            &triple,
            "generic",
            "",
            opt_level.to_llvm(),
            RelocMode::Default,
            CodeModel::Default,
        )
        .ok_or(CodegenError::OutputFailed {
            artifact: "target-machine".into(),
            message: "failed to create target machine".into(),
        })
}
