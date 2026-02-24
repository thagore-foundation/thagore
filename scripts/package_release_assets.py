import argparse
import hashlib
import json
import shutil
import tarfile
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
REGISTRY = ROOT / "targets" / "registry" / "targets.json"


def _normalize_host(host: str) -> str:
    t = host.strip().lower()
    if t in {"linux", "ubuntu", "ubuntu-latest"}:
        return "linux"
    if t in {"windows", "windows-latest"}:
        return "windows"
    if t in {"macos", "macos-latest", "darwin"}:
        return "macos"
    return t or "unknown"


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        while True:
            chunk = f.read(1024 * 1024)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()


def _write_json(path: Path, obj: dict) -> None:
    path.write_text(json.dumps(obj, indent=2) + "\n", encoding="utf-8")


def _copy_if_exists(src: Path, dst: Path) -> None:
    if src.exists() and src.is_file():
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(src.read_bytes())


def _build_core_bundle(host: str, out_dir: Path, version: str) -> Path:
    bundle_stem = f"thagc-core-{host}"
    stage_dir = Path(tempfile.mkdtemp(prefix=f"{bundle_stem}-"))

    meta = {
        "schema": "thagc.release.core.bundle.v1",
        "host": host,
        "version": version,
        "artifacts": [
            "scripts/install/thagup-init.sh",
            "scripts/toolchain_config.py",
            "scripts/target_pack_store.py",
            "targets/registry/targets.json",
        ],
    }
    _write_json(stage_dir / "manifest.json", meta)
    _copy_if_exists(ROOT / "scripts" / "install" / "thagup-init.sh", stage_dir / "scripts" / "install" / "thagup-init.sh")
    _copy_if_exists(ROOT / "scripts" / "toolchain_config.py", stage_dir / "scripts" / "toolchain_config.py")
    _copy_if_exists(ROOT / "scripts" / "target_pack_store.py", stage_dir / "scripts" / "target_pack_store.py")
    _copy_if_exists(ROOT / "targets" / "registry" / "targets.json", stage_dir / "targets" / "registry" / "targets.json")

    compiler_candidates = [
        ROOT / "stage2",
        ROOT / "stage2.exe",
        ROOT / "stage1",
        ROOT / "stage1.exe",
        ROOT / "thagore",
        ROOT / "thagore.exe",
    ]
    for candidate in compiler_candidates:
        if candidate.exists() and candidate.is_file():
            name = "thagc.exe" if candidate.suffix.lower() == ".exe" else "thagc"
            _copy_if_exists(candidate, stage_dir / "bin" / name)
            break

    tar_path = out_dir / f"{bundle_stem}.tar.gz"
    with tarfile.open(tar_path, "w:gz") as tf:
        tf.add(stage_dir, arcname=bundle_stem)
    shutil.rmtree(stage_dir, ignore_errors=True)
    return tar_path


def _build_target_bundle(host: str, out_dir: Path, version: str, triple: str) -> Path:
    src_pack = ROOT / "targets" / "packs" / triple
    if not src_pack.exists() or not src_pack.is_dir():
        raise RuntimeError(f"missing target pack directory: {src_pack}")

    bundle_stem = f"thagc-target-{triple}-{host}"
    stage_dir = Path(tempfile.mkdtemp(prefix=f"{bundle_stem}-"))

    _write_json(
        stage_dir / "manifest.json",
        {
            "schema": "thagc.release.target.bundle.v1",
            "host": host,
            "version": version,
            "target": triple,
            "source_pack": str(src_pack.relative_to(ROOT)),
        },
    )
    pack_dst = stage_dir / "pack"
    pack_dst.mkdir(parents=True, exist_ok=True)
    for path in src_pack.rglob("*"):
        rel = path.relative_to(src_pack)
        dst = pack_dst / rel
        if path.is_dir():
            dst.mkdir(parents=True, exist_ok=True)
        else:
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(path.read_bytes())

    tar_path = out_dir / f"{bundle_stem}.tar.gz"
    with tarfile.open(tar_path, "w:gz") as tf:
        tf.add(stage_dir, arcname=bundle_stem)
    shutil.rmtree(stage_dir, ignore_errors=True)
    return tar_path


def main() -> int:
    parser = argparse.ArgumentParser(description="Package thagc core and target bundles.")
    parser.add_argument("--host-os", required=True, help="linux|windows|macos")
    parser.add_argument("--version", default="dev")
    parser.add_argument("--out-dir", default="dist/release")
    parser.add_argument("--report", default="release-assets-report.txt")
    args = parser.parse_args()

    host = _normalize_host(args.host_os)
    out_dir = (ROOT / args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    registry = json.loads(REGISTRY.read_text(encoding="utf-8"))
    triples = [str(row.get("triple", "")).strip() for row in registry.get("targets", [])]
    triples = [t for t in triples if t]

    built: list[Path] = []
    built.append(_build_core_bundle(host, out_dir, args.version))
    for triple in triples:
        built.append(_build_target_bundle(host, out_dir, args.version, triple))

    sums = out_dir / f"SHA256SUMS-thagc-{host}.txt"
    lines: list[str] = []
    for path in built:
        lines.append(f"{_sha256(path)}  {path.name}")
    sums.write_text("\n".join(lines) + "\n", encoding="utf-8")

    report = ROOT / args.report
    rows = [
        "=== Release Assets Report ===",
        f"host={host}",
        f"version={args.version}",
        f"out_dir={out_dir}",
    ]
    for path in built:
        rows.append(f"asset={path.name}")
    rows.append(f"checksums={sums.name}")
    rows.append("")
    report.write_text("\n".join(rows), encoding="utf-8")
    print("\n".join(rows))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
