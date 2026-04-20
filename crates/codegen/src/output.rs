//! Output artifact emission for the Thagore LLVM backend.

use std::env;
use std::fs;
use std::process::Command;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

use inkwell::module::Module;
use inkwell::targets::{FileType, TargetMachine};

use crate::context::{create_target_machine, TargetMachineConfig};
use crate::error::CodegenError;
use crate::optimize::OptimizationLevel;

const EMBEDDED_RUNTIME_SOURCE: &str = include_str!("../runtime/thagore_rt.c");

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
    target: &TargetMachineConfig,
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
        match create_target_machine(target, opt_level) {
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
    link_objects(core::slice::from_ref(&object.to_path_buf()), binary)
}

/// Links multiple object files into a native executable via `cc`.
pub fn link_objects(objects: &[PathBuf], binary: &Path) -> Result<(), CodegenError> {
    let runtime_object = compile_runtime_object(binary)?;
    let mut attempts = linker_candidates();
    attempts.push(Linker::SystemCc);
    let compiler = c_compiler();

    let mut last_error = None;
    for linker in attempts {
        match try_link(&compiler, linker, objects, &runtime_object, binary) {
            Ok(()) => return Ok(()),
            Err(error) => last_error = Some(error),
        }
    }

    Err(last_error.unwrap_or(CodegenError::LinkFailed {
        linker: "cc".into(),
        message: "no usable linker found".into(),
    }))
}

fn compile_runtime_object(binary: &Path) -> Result<PathBuf, CodegenError> {
    let nonce = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|duration| duration.as_nanos())
        .unwrap_or_default();
    let runtime_source = env::temp_dir().join(format!(
        "thagore_rt_{}_{}.c",
        std::process::id(),
        nonce
    ));
    let runtime_object = binary.with_extension("thagore_rt.o");
    fs::write(&runtime_source, EMBEDDED_RUNTIME_SOURCE).map_err(|error| CodegenError::LinkFailed {
        linker: "cc".into(),
        message: error.to_string(),
    })?;
    let compiler = c_compiler();
    let output = Command::new(&compiler)
        .arg("-c")
        .arg("-O2")
        .arg(command_path(&runtime_source))
        .arg("-o")
        .arg(command_path(&runtime_object))
        .output()
        .map_err(|error| CodegenError::LinkFailed {
            linker: compiler.clone(),
            message: error.to_string(),
        })?;

    let _ = fs::remove_file(&runtime_source);

    if output.status.success() {
        Ok(runtime_object)
    } else {
        Err(CodegenError::LinkFailed {
            linker: compiler,
            message: String::from_utf8_lossy(&output.stderr).into_owned(),
        })
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Linker {
    Mold,
    Lld,
    SystemCc,
}

fn linker_candidates() -> Vec<Linker> {
    let mut linkers = Vec::new();
    if which("mold") {
        linkers.push(Linker::Mold);
    }
    if which("ld.lld") || which("lld") {
        linkers.push(Linker::Lld);
    }
    linkers
}

fn try_link(
    compiler: &str,
    linker: Linker,
    objects: &[PathBuf],
    runtime_object: &Path,
    binary: &Path,
) -> Result<(), CodegenError> {
    let mut command = Command::new(compiler);
    for object in objects {
        command.arg(command_path(object));
    }
    command.arg(command_path(runtime_object));
    if !cfg!(windows) {
        command.arg("-lm");
    }
    command.arg("-o").arg(command_path(binary));
    let linker_name = match linker {
        Linker::Mold => {
            command.arg("-fuse-ld=mold");
            "mold"
        }
        Linker::Lld => {
            command.arg("-fuse-ld=lld");
            "lld"
        }
        Linker::SystemCc => "cc",
    };
    if cfg!(windows) {
        command.arg("-Wl,/Brepro");
    } else {
        command.arg("-Wl,--build-id=none");
    }

    let output = command.output().map_err(|error| CodegenError::LinkFailed {
        linker: linker_name.into(),
        message: error.to_string(),
    })?;
    if output.status.success() {
        return Ok(());
    }

    Err(CodegenError::LinkFailed {
        linker: linker_name.into(),
        message: String::from_utf8_lossy(&output.stderr).into_owned(),
    })
}

fn c_compiler() -> String {
    if let Some(compiler) = env::var_os("CC").filter(|value| !value.is_empty()) {
        return compiler.to_string_lossy().into_owned();
    }

    if cfg!(windows) && which("clang") {
        return "clang".into();
    }

    "cc".into()
}

fn command_path(path: &Path) -> String {
    if cfg!(windows) {
        path.to_string_lossy().replace('\\', "/")
    } else {
        path.to_string_lossy().into_owned()
    }
}

fn which(program: &str) -> bool {
    let Some(paths) = env::var_os("PATH") else {
        return false;
    };

    env::split_paths(&paths).any(|path| {
        let candidate = path.join(program);
        candidate.is_file() || has_windows_exe_suffix(&candidate)
    })
}

fn has_windows_exe_suffix(path: &Path) -> bool {
    if cfg!(windows) {
        return path.with_extension("exe").is_file();
    }
    let _ = path;
    false
}
