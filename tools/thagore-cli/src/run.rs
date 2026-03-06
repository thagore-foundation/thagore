//! Temporary build directory management and process execution for `thagore run`.

use std::ffi::OsString;
use std::io;
use std::path::{Path, PathBuf};
use std::process::Command;

use tempfile::{Builder, TempDir};

/// RAII wrapper around the temporary workspace used by `thagore run`.
#[derive(Debug)]
pub(crate) struct RunWorkspace {
    dir: TempDir,
}

impl RunWorkspace {
    /// Creates a new temporary workspace under the system temporary directory.
    pub(crate) fn new() -> io::Result<Self> {
        let dir = Builder::new().prefix("thagore-").tempdir()?;
        Ok(Self { dir })
    }

    /// Returns the workspace directory path.
    #[must_use]
    pub(crate) fn path(&self) -> &Path {
        self.dir.path()
    }

    /// Derives the binary output path for a given source file.
    #[must_use]
    pub(crate) fn binary_path(&self, input: &Path) -> PathBuf {
        let stem = input
            .file_stem()
            .and_then(|stem| stem.to_str())
            .filter(|stem| !stem.is_empty())
            .unwrap_or("thagore-run");
        self.path().join(stem)
    }
}

/// Executes a compiled program and returns its forwarded exit code.
#[cfg_attr(test, allow(dead_code))]
pub(crate) fn execute_binary(binary: &Path, args: &[OsString]) -> io::Result<i32> {
    let status = Command::new(binary).args(args).status()?;
    Ok(status.code().unwrap_or(1))
}

#[cfg(test)]
mod tests {
    use super::RunWorkspace;
    use std::fs;
    use std::path::Path;

    #[test]
    fn temp_workspace_is_removed_on_drop() {
        let path = {
            let workspace = RunWorkspace::new().expect("workspace");
            let path = workspace.path().to_path_buf();
            assert!(path.exists());
            path
        };

        assert!(!path.exists());
    }

    #[test]
    fn binary_path_uses_source_stem() {
        let workspace = RunWorkspace::new().expect("workspace");
        let binary = workspace.binary_path(Path::new("examples/hello.tg"));

        assert_eq!(
            binary.file_name().and_then(|name| name.to_str()),
            Some("hello")
        );
        assert!(fs::metadata(workspace.path()).is_ok());
    }
}
