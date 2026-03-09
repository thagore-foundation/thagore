//! Self-update command execution for the Thagore toolchain.

use std::ffi::OsString;
use std::path::{Path, PathBuf};
use std::process::Command;

use tempfile::NamedTempFile;

use crate::cli::SelfUpdateArgs;

const INSTALLER_REPO_BASE: &str =
    "https://raw.githubusercontent.com/thagore-foundation/thagore/main/tooling/release";

/// Runs the platform-appropriate installer with the selected self-update flags.
pub fn run(args: &SelfUpdateArgs) -> Result<(), String> {
    #[cfg(windows)]
    {
        run_windows(args)
    }
    #[cfg(not(windows))]
    {
        run_posix(args)
    }
}

#[cfg(not(windows))]
fn run_posix(args: &SelfUpdateArgs) -> Result<(), String> {
    let script_path = ensure_script("thagup.sh")?;
    let status = Command::new("bash")
        .arg(script_path)
        .args(posix_args(args))
        .status()
        .map_err(|error| format!("failed to execute self-update installer: {error}"))?;
    if status.success() {
        Ok(())
    } else {
        Err(format!(
            "self-update installer exited with {}",
            status.code().unwrap_or(1)
        ))
    }
}

#[cfg(windows)]
fn run_windows(args: &SelfUpdateArgs) -> Result<(), String> {
    let script_path = ensure_script("thagup.ps1")?;
    let status = Command::new("powershell")
        .arg("-NoProfile")
        .arg("-ExecutionPolicy")
        .arg("Bypass")
        .arg("-File")
        .arg(script_path)
        .args(windows_args(args))
        .status()
        .map_err(|error| format!("failed to execute self-update installer: {error}"))?;
    if status.success() {
        Ok(())
    } else {
        Err(format!(
            "self-update installer exited with {}",
            status.code().unwrap_or(1)
        ))
    }
}

fn ensure_script(name: &str) -> Result<PathBuf, String> {
    if let Some(local) = installed_script_path(name).filter(|path| path.is_file()) {
        return Ok(local);
    }
    download_script(name)
}

fn installed_script_path(name: &str) -> Option<PathBuf> {
    let executable = std::env::current_exe().ok()?;
    let bin_dir = executable.parent()?;
    let share = bin_dir.parent()?.join("share/thagore/install").join(name);
    if share.is_file() {
        return Some(share);
    }
    let fallback = Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../../tooling/release")
        .join(name);
    fallback.is_file().then_some(fallback)
}

fn download_script(name: &str) -> Result<PathBuf, String> {
    let url = format!("{INSTALLER_REPO_BASE}/{name}");
    let temp = NamedTempFile::new().map_err(|error| error.to_string())?;
    let path = temp.into_temp_path();
    let path_buf = path.to_path_buf();
    let status = Command::new("curl")
        .arg("-fsSL")
        .arg(&url)
        .arg("-o")
        .arg(&path_buf)
        .status()
        .map_err(|error| format!("failed to download installer: {error}"))?;
    if !status.success() {
        return Err(format!("failed to download installer from {url}"));
    }
    path.keep().map_err(|error| error.error.to_string())?;
    Ok(path_buf)
}

#[cfg(not(windows))]
fn posix_args(args: &SelfUpdateArgs) -> Vec<OsString> {
    let mut output = Vec::new();
    if args.force {
        output.push(OsString::from("--force"));
    }
    push_common_args(
        &mut output,
        args,
        "--channel",
        "--target",
        "--arch",
        "--prefix",
        "--tag",
        "--drago-tag",
    );
    if args.without_drago {
        output.push(OsString::from("--without-drago"));
    }
    if args.dry_run {
        output.push(OsString::from("--dry-run"));
    }
    output
}

#[cfg(windows)]
fn windows_args(args: &SelfUpdateArgs) -> Vec<OsString> {
    let mut output = Vec::new();
    if args.force {
        output.push(OsString::from("-Force"));
    }
    push_common_args(
        &mut output,
        args,
        "-Channel",
        "-Target",
        "-Arch",
        "-Prefix",
        "-Tag",
        "-DragoTag",
    );
    if args.without_drago {
        output.push(OsString::from("-WithoutDrago"));
    }
    if args.dry_run {
        output.push(OsString::from("-DryRun"));
    }
    output
}

fn push_common_args(
    output: &mut Vec<OsString>,
    args: &SelfUpdateArgs,
    channel_flag: &str,
    target_flag: &str,
    arch_flag: &str,
    prefix_flag: &str,
    tag_flag: &str,
    drago_tag_flag: &str,
) {
    output.push(OsString::from(channel_flag));
    output.push(OsString::from(args.channel.as_ref()));

    if let Some(target) = &args.target {
        output.push(OsString::from(target_flag));
        output.push(OsString::from(target));
    }
    if let Some(arch) = &args.arch {
        output.push(OsString::from(arch_flag));
        output.push(OsString::from(arch));
    }
    if let Some(prefix) = &args.prefix {
        output.push(OsString::from(prefix_flag));
        output.push(prefix.as_os_str().to_os_string());
    }
    if let Some(tag) = &args.tag {
        output.push(OsString::from(tag_flag));
        output.push(OsString::from(tag));
    }
    if let Some(tag) = &args.drago_tag {
        output.push(OsString::from(drago_tag_flag));
        output.push(OsString::from(tag));
    }
}

#[cfg(test)]
mod tests {
    use super::push_common_args;
    use crate::cli::{ReleaseChannel, SelfUpdateArgs};
    use std::ffi::OsString;
    use std::path::PathBuf;

    #[test]
    fn common_args_preserve_cli_values() {
        let args = SelfUpdateArgs {
            channel: ReleaseChannel::Extended,
            target: Some("x86_64-unknown-linux-musl".into()),
            arch: Some("x86_64".into()),
            prefix: Some(PathBuf::from("/tmp/toolchain")),
            tag: Some("v0.9.2".into()),
            drago_tag: Some("v1.0.7".into()),
            without_drago: false,
            dry_run: true,
            force: true,
        };
        let mut output = Vec::<OsString>::new();
        push_common_args(
            &mut output,
            &args,
            "--channel",
            "--target",
            "--arch",
            "--prefix",
            "--tag",
            "--drago-tag",
        );
        assert!(output.contains(&OsString::from("extended")));
        assert!(output.contains(&OsString::from("x86_64-unknown-linux-musl")));
        assert!(output.contains(&OsString::from("x86_64")));
        assert!(output.contains(&OsString::from("/tmp/toolchain")));
        assert!(output.contains(&OsString::from("v0.9.2")));
        assert!(output.contains(&OsString::from("v1.0.7")));
    }
}
