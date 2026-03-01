#!/usr/bin/env python3
import argparse
import os
import platform
import re
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path


def detect_mode(raw_mode: str) -> str:
    if raw_mode != "auto":
        return raw_mode
    system = platform.system().lower()
    if system == "linux":
        return "linux"
    if system == "darwin":
        return "macos"
    if system == "windows":
        return "windows"
    raise RuntimeError(f"unsupported host platform: {platform.system()}")


def run_capture(args: list[str]) -> str:
    proc = subprocess.run(args, capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        raise RuntimeError(f"command failed ({proc.returncode}): {' '.join(args)}\n{proc.stderr}")
    return proc.stdout


def parse_ldd_deps(bin_path: Path) -> list[Path]:
    output = run_capture(["ldd", str(bin_path)])
    deps: list[Path] = []
    for raw in output.splitlines():
        line = raw.strip()
        if not line or "linux-vdso.so" in line:
            continue
        candidate = ""
        if "=>" in line:
            rhs = line.split("=>", 1)[1].strip()
            if rhs == "not found":
                raise RuntimeError(f"missing shared library dependency: {line}")
            candidate = rhs.split(" ", 1)[0].strip()
        elif line.startswith("/"):
            candidate = line.split(" ", 1)[0].strip()
        if not candidate:
            continue
        path = Path(candidate)
        if not path.exists():
            raise RuntimeError(f"missing shared library dependency: {candidate}")
        name = path.name
        if (
            name.startswith("ld-linux")
            or name.startswith("libc.so")
            or name.startswith("libm.so")
            or name.startswith("libpthread.so")
            or name.startswith("librt.so")
            or name.startswith("libdl.so")
            or name.startswith("libutil.so")
            or name.startswith("libresolv.so")
        ):
            continue
        deps.append(path)
    return sorted(set(deps))


def parse_otool_deps(bin_path: Path) -> list[Path]:
    output = run_capture(["otool", "-L", str(bin_path)])
    deps: list[Path] = []
    for idx, raw in enumerate(output.splitlines()):
        if idx == 0:
            continue
        line = raw.strip()
        if not line:
            continue
        candidate = line.split(" ", 1)[0].strip()
        if not candidate.startswith("/"):
            continue
        if candidate.startswith("/usr/lib/") or candidate.startswith("/System/"):
            continue
        path = Path(candidate)
        if not path.exists():
            raise RuntimeError(f"missing dylib dependency: {candidate}")
        deps.append(path)
    return sorted(set(deps))


def parse_dumpbin_dependents(bin_path: Path) -> list[str]:
    try:
        output = run_capture(["dumpbin", "/nologo", "/dependents", str(bin_path)])
    except (RuntimeError, FileNotFoundError):
        try:
            output = run_capture(["llvm-objdump", "-p", str(bin_path)])
        except (RuntimeError, FileNotFoundError):
            # Some Windows runners do not expose dumpbin/llvm-objdump in PATH.
            # In that case, skip dependency scanning and rely on the bare binary.
            return []
    dll_names: list[str] = []
    for raw in output.splitlines():
        line = raw.strip()
        if not line:
            continue
        m = re.search(r"DLL Name:\s*([A-Za-z0-9_.-]+\.dll)", line, re.IGNORECASE)
        if m:
            dll_names.append(m.group(1))
            continue
        if line.upper().endswith(".DLL"):
            dll_names.append(Path(line).name)
    unique = sorted({name.lower(): name for name in dll_names}.values(), key=lambda x: x.lower())
    return unique


def find_dll_on_host(dll_name: str, search_roots: list[Path]) -> Path | None:
    for root in search_roots:
        candidate = root / dll_name
        if candidate.exists():
            return candidate
    return None


def collect_windows_deps(bin_path: Path) -> list[Path]:
    dlls = parse_dumpbin_dependents(bin_path)
    if not dlls:
        return []

    search_roots: list[Path] = [bin_path.parent.resolve()]
    llvm_dir = os.environ.get("LLVM_DIR", "").strip()
    if llvm_dir:
        llvm_path = Path(llvm_dir)
        # LLVM_DIR is usually .../lib/cmake/llvm.
        maybe_bin = (llvm_path / ".." / ".." / ".." / "bin").resolve()
        if maybe_bin.exists():
            search_roots.append(maybe_bin)
    path_env = os.environ.get("PATH", "")
    for part in path_env.split(os.pathsep):
        if not part:
            continue
        p = Path(part)
        if p.exists():
            search_roots.append(p.resolve())

    deps: list[Path] = []
    missing: list[str] = []
    for dll in dlls:
        low = dll.lower()
        if low.startswith("kernel32") or low.startswith("user32") or low.startswith("advapi32") or low.startswith(
            "ntdll"
        ):
            continue
        found = find_dll_on_host(dll, search_roots)
        if found is None:
            missing.append(dll)
            continue
        deps.append(found)
    if missing:
        raise RuntimeError(f"missing DLL dependencies: {', '.join(missing)}")
    return sorted(set(deps))


def collect_runtime_deps(bin_path: Path, mode: str) -> list[Path]:
    if mode == "linux":
        return parse_ldd_deps(bin_path)
    if mode == "macos":
        return parse_otool_deps(bin_path)
    if mode == "windows":
        return collect_windows_deps(bin_path)
    raise RuntimeError(f"unsupported packaging mode: {mode}")


def write_posix_launcher(stage_bin: Path) -> None:
    launcher = stage_bin / "thagc"
    launcher.write_text(
        "#!/usr/bin/env bash\n"
        "set -euo pipefail\n"
        "SELF_DIR=\"$(cd \"$(dirname \"${BASH_SOURCE[0]}\")\" && pwd)\"\n"
        "LIB_DIR=\"${SELF_DIR}/../lib\"\n"
        "case \"$(uname -s)\" in\n"
        "  Linux)\n"
        "    if [[ -d \"${LIB_DIR}\" ]]; then\n"
        "      export LD_LIBRARY_PATH=\"${LIB_DIR}:${LD_LIBRARY_PATH:-}\"\n"
        "    fi\n"
        "    ;;\n"
        "  Darwin)\n"
        "    if [[ -d \"${LIB_DIR}\" ]]; then\n"
        "      export DYLD_LIBRARY_PATH=\"${LIB_DIR}:${DYLD_LIBRARY_PATH:-}\"\n"
        "    fi\n"
        "    ;;\n"
        "esac\n"
        "exec \"${SELF_DIR}/thagc.real\" \"$@\"\n"
    )
    launcher.chmod(0o755)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bin", required=True)
    parser.add_argument("--runtime", required=False, default="")
    parser.add_argument("--out", required=True)
    parser.add_argument("--bundle-deps", action="store_true")
    parser.add_argument("--mode", choices=["auto", "linux", "macos", "windows"], default="auto")
    args = parser.parse_args()

    out = Path(args.out)
    bin_path = Path(args.bin)
    mode = detect_mode(args.mode)
    if not bin_path.exists():
        raise RuntimeError(f"binary not found: {bin_path}")

    out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="thag-pack-core-") as td:
        stage = Path(td)
        stage_bin = stage / "bin"
        stage_lib = stage / "lib"
        stage_bin.mkdir(parents=True, exist_ok=True)
        stage_lib.mkdir(parents=True, exist_ok=True)

        if mode == "windows":
            staged_binary = stage_bin / "thagc.exe"
        else:
            staged_binary = stage_bin / "thagc.real"
        shutil.copy2(bin_path, staged_binary)
        staged_binary.chmod(0o755)

        if mode != "windows":
            write_posix_launcher(stage_bin)

        if args.runtime:
            runtime_path = Path(args.runtime)
            if runtime_path.exists():
                shutil.copy2(runtime_path, stage_lib / runtime_path.name)

        if args.bundle_deps:
            deps = collect_runtime_deps(bin_path, mode)
            dep_dst = stage_bin if mode == "windows" else stage_lib
            for dep in deps:
                shutil.copy2(dep, dep_dst / dep.name)

        with tarfile.open(out, "w:gz") as tf:
            tf.add(stage, arcname=".")
    print(f"created {out}")


if __name__ == "__main__":
    main()
