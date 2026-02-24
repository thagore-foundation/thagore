import argparse
import json
import struct
import subprocess
import sys
import tarfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
REQUIRED = {
    "linux-x86_64": (
        "thagore-stage1-linux-x86_64.tar.gz",
        "thagore-runtime-linux-x86_64.a",
        "seed-promotion-manifest-linux-x86_64.txt",
        "seed-stage1-provenance-linux-x86_64.json",
        "seed-bundle-linux-x86_64.json",
    ),
    "linux-arm64": (
        "thagore-stage1-linux-arm64.tar.gz",
        "thagore-runtime-linux-arm64.a",
        "seed-promotion-manifest-linux-arm64.txt",
        "seed-stage1-provenance-linux-arm64.json",
        "seed-bundle-linux-arm64.json",
    ),
    "macos-arm64": (
        "thagore-stage1-macos-arm64.tar.gz",
        "thagore-runtime-macos-arm64.a",
        "seed-promotion-manifest-macos-arm64.txt",
        "seed-stage1-provenance-macos-arm64.json",
        "seed-bundle-macos-arm64.json",
    ),
    "windows-x86_64": (
        "thagore-stage1-windows-x86_64.exe",
        "thagore-runtime-windows-x86_64.lib",
        "seed-promotion-manifest-windows-x86_64.txt",
        "seed-stage1-provenance-windows-x86_64.json",
        "seed-bundle-windows-x86_64.json",
    ),
    "windows-arm64": (
        "thagore-stage1-windows-arm64.exe",
        "thagore-runtime-windows-arm64.lib",
        "seed-promotion-manifest-windows-arm64.txt",
        "seed-stage1-provenance-windows-arm64.json",
        "seed-bundle-windows-arm64.json",
    ),
}

ARCH_EXPECTED = {
    "linux-x86_64": "x86_64",
    "linux-arm64": "arm64",
    "macos-arm64": "arm64",
    "windows-x86_64": "x86_64",
    "windows-arm64": "arm64",
}


def _run(cmd: list[str]) -> tuple[int, str]:
    proc = subprocess.run(
        cmd,
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )
    out = (proc.stdout or "") + (proc.stderr or "")
    return proc.returncode, out.strip()


def _find_single(root: Path, name: str) -> Path:
    hits = sorted(p for p in root.rglob(name) if p.is_file())
    if len(hits) != 1:
        raise ValueError(f"expected exactly one '{name}', found={len(hits)}")
    return hits[0]


def _parse_manifest(path: Path) -> dict[str, str]:
    out: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or "=" not in line:
            continue
        k, v = line.split("=", 1)
        k = k.strip()
        v = v.strip()
        if k and k not in out:
            out[k] = v
    return out


def _detect_pe_arch(blob: bytes) -> set[str]:
    if len(blob) < 0x40 or blob[:2] != b"MZ":
        return set()
    pe_off = struct.unpack_from("<I", blob, 0x3C)[0]
    if pe_off + 6 > len(blob) or blob[pe_off : pe_off + 4] != b"PE\x00\x00":
        return set()
    machine = struct.unpack_from("<H", blob, pe_off + 4)[0]
    if machine == 0x8664:
        return {"x86_64"}
    if machine == 0xAA64:
        return {"arm64"}
    return {f"pe-machine-0x{machine:04x}"}


def _detect_elf_arch(blob: bytes) -> set[str]:
    if len(blob) < 20 or blob[:4] != b"\x7fELF":
        return set()
    ei_data = blob[5]
    fmt = "<H" if ei_data == 1 else ">H"
    machine = struct.unpack_from(fmt, blob, 18)[0]
    if machine == 62:
        return {"x86_64"}
    if machine == 183:
        return {"arm64"}
    return {f"elf-machine-{machine}"}


def _detect_macho_arch(blob: bytes) -> set[str]:
    if len(blob) < 8:
        return set()
    magic_be = struct.unpack_from(">I", blob, 0)[0]
    out: set[str] = set()
    if magic_be in (0xCAFEBABE, 0xCAFEBABF):
        nfat = struct.unpack_from(">I", blob, 4)[0]
        off = 8
        step = 20 if magic_be == 0xCAFEBABE else 32
        for _ in range(nfat):
            if off + 8 > len(blob):
                break
            cputype = struct.unpack_from(">I", blob, off)[0]
            if cputype == 0x01000007:
                out.add("x86_64")
            elif cputype == 0x0100000C:
                out.add("arm64")
            else:
                out.add(f"macho-cputype-{cputype}")
            off += step
        return out

    if magic_be in (0xFEEDFACE, 0xFEEDFACF):
        cputype = struct.unpack_from(">I", blob, 4)[0]
    else:
        magic_le = struct.unpack_from("<I", blob, 0)[0]
        if magic_le not in (0xFEEDFACE, 0xFEEDFACF):
            return set()
        cputype = struct.unpack_from("<I", blob, 4)[0]
    if cputype == 0x01000007:
        return {"x86_64"}
    if cputype == 0x0100000C:
        return {"arm64"}
    return {f"macho-cputype-{cputype}"}


def _read_stage1_blob(asset_tag: str, stage1_path: Path) -> tuple[bytes, str]:
    if asset_tag.startswith("windows-"):
        return stage1_path.read_bytes(), stage1_path.name

    with tarfile.open(stage1_path, "r:*") as tf:
        members = [m for m in tf.getmembers() if m.isfile()]
        preferred = []
        for name in ("thagore", "thag", "stage1"):
            preferred.extend([m for m in members if Path(m.name).name == name])
        picks = preferred if preferred else members
        for member in picks:
            ex = tf.extractfile(member)
            if ex is None:
                continue
            blob = ex.read()
            if blob:
                return blob, member.name
    raise ValueError(f"{asset_tag}: stage1 archive has no readable binary payload")


def _verify_stage1_arch(asset_tag: str, stage1_path: Path) -> tuple[bool, str]:
    expected = ARCH_EXPECTED.get(asset_tag, "")
    blob, member_name = _read_stage1_blob(asset_tag, stage1_path)
    if asset_tag.startswith("windows-"):
        detected = _detect_pe_arch(blob)
    elif asset_tag.startswith("linux-"):
        detected = _detect_elf_arch(blob)
    elif asset_tag.startswith("macos-"):
        detected = _detect_macho_arch(blob)
    else:
        detected = set()
    if not detected:
        return False, f"{asset_tag}: unable to detect stage1 arch from {member_name}"
    if expected and expected not in detected:
        det = ",".join(sorted(detected))
        return (
            False,
            f"{asset_tag}: stage1 arch mismatch expected={expected} detected={det} source={member_name}",
        )
    det = ",".join(sorted(detected))
    return True, f"{asset_tag}: stage1 arch verified expected={expected} detected={det} source={member_name}"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify atomic seed release bundles for all supported OS/arch lanes."
    )
    parser.add_argument("--root", required=True, help="Directory containing downloaded release assets")
    parser.add_argument("--expected-seed-tag", required=True)
    parser.add_argument("--report", default="seed-release-bundle-verify.txt")
    args = parser.parse_args()

    root = Path(args.root)
    expected_seed_tag = args.expected_seed_tag.strip()
    report_path = ROOT / args.report

    rows: list[str] = []
    errors: list[str] = []
    commits: set[str] = set()
    repositories: set[str] = set()

    if not root.exists():
        errors.append(f"missing root: {root}")

    for asset_tag, names in REQUIRED.items():
        stage1_name, runtime_name, manifest_name, prov_name, bundle_name = names
        try:
            stage1_path = _find_single(root, stage1_name)
            runtime_path = _find_single(root, runtime_name)
            manifest_path = _find_single(root, manifest_name)
            prov_path = _find_single(root, prov_name)
            bundle_path = _find_single(root, bundle_name)
        except Exception as exc:
            errors.append(f"{asset_tag}: {exc}")
            continue

        rows.append(f"OK|located|{asset_tag}|stage1={stage1_path.name}")
        rows.append(f"OK|located|{asset_tag}|runtime={runtime_path.name}")
        rows.append(f"OK|located|{asset_tag}|manifest={manifest_path.name}")
        rows.append(f"OK|located|{asset_tag}|provenance={prov_path.name}")
        rows.append(f"OK|located|{asset_tag}|bundle={bundle_path.name}")
        ok_arch, arch_msg = _verify_stage1_arch(asset_tag, stage1_path)
        if not ok_arch:
            errors.append(arch_msg)
            continue
        rows.append(f"OK|stage1_arch|{arch_msg}")

        manifest = _parse_manifest(manifest_path)
        if manifest.get("schema", "") != "thagore.seed.promote.v1":
            errors.append(f"{asset_tag}: manifest schema mismatch")
            continue
        if manifest.get("seed_tag", "") != expected_seed_tag:
            errors.append(
                f"{asset_tag}: manifest seed_tag mismatch expected={expected_seed_tag} got={manifest.get('seed_tag','')}"
            )
            continue
        if manifest.get("asset_tag", "") != asset_tag:
            errors.append(
                f"{asset_tag}: manifest asset_tag mismatch got={manifest.get('asset_tag','')}"
            )
            continue
        rows.append(f"OK|manifest_meta|{asset_tag}")

        rc, out = _run(
            [
                sys.executable,
                "scripts/seed_bundle.py",
                "verify",
                "--bundle",
                str(bundle_path),
                "--manifest",
                str(manifest_path),
                "--provenance",
                str(prov_path),
            ]
        )
        if rc != 0:
            errors.append(f"{asset_tag}: seed_bundle verify failed: {out}")
            continue
        rows.append(f"OK|seed_bundle_verify|{asset_tag}")

        try:
            bundle_obj = json.loads(bundle_path.read_text(encoding="utf-8"))
            prov_obj = json.loads(prov_path.read_text(encoding="utf-8"))
        except Exception as exc:
            errors.append(f"{asset_tag}: invalid json: {exc}")
            continue

        bundle_meta = bundle_obj.get("metadata") or {}
        prov_meta = prov_obj.get("metadata") or {}
        bundle_commit = str(bundle_meta.get("commit_sha", "")).strip()
        prov_commit = str(prov_meta.get("commit_sha", "")).strip()
        bundle_id = str(bundle_meta.get("bundle_id", "")).strip().lower()
        bundle_seed_tag = str(bundle_meta.get("seed_tag", "")).strip()
        prov_seed_tag = str(prov_meta.get("seed_tag", "")).strip()
        bundle_asset_tag = str(bundle_meta.get("asset_tag", "")).strip()
        prov_asset_tag = str(prov_meta.get("asset_tag", "")).strip()
        bundle_repo = str(bundle_meta.get("repository", "")).strip()
        prov_repo = str(prov_meta.get("repository", "")).strip()

        if not bundle_commit or not prov_commit or bundle_commit != prov_commit:
            errors.append(
                f"{asset_tag}: commit mismatch bundle={bundle_commit} provenance={prov_commit}"
            )
            continue
        commits.add(bundle_commit)
        if bundle_seed_tag != expected_seed_tag or prov_seed_tag != expected_seed_tag:
            errors.append(
                f"{asset_tag}: seed_tag mismatch bundle={bundle_seed_tag} provenance={prov_seed_tag} expected={expected_seed_tag}"
            )
            continue
        if bundle_asset_tag != asset_tag or prov_asset_tag != asset_tag:
            errors.append(
                f"{asset_tag}: asset_tag mismatch bundle={bundle_asset_tag} provenance={prov_asset_tag}"
            )
            continue
        if bundle_repo:
            repositories.add(bundle_repo)
        if prov_repo and bundle_repo and prov_repo != bundle_repo:
            errors.append(
                f"{asset_tag}: repository mismatch bundle={bundle_repo} provenance={prov_repo}"
            )
            continue

        rc, out = _run(
            [
                sys.executable,
                "scripts/stage1_provenance.py",
                "verify",
                "--provenance",
                str(prov_path),
                "--manifest",
                str(manifest_path),
                "--asset",
                str(stage1_path),
                "--asset",
                str(runtime_path),
                "--expected-commit-sha",
                bundle_commit,
                "--expected-bundle-id",
                bundle_id,
            ]
        )
        if rc != 0:
            errors.append(f"{asset_tag}: stage1_provenance verify failed: {out}")
            continue
        rows.append(f"OK|stage1_provenance_verify|{asset_tag}")

    if len(commits) != 1:
        errors.append(f"commit mismatch across lanes: {sorted(commits)}")
    else:
        rows.append(f"OK|commit_consistent|{next(iter(commits))}")

    if len(repositories) > 1:
        errors.append(f"repository mismatch across lanes: {sorted(repositories)}")
    elif len(repositories) == 1:
        rows.append(f"OK|repository_consistent|{next(iter(repositories))}")

    status = "pass" if not errors else "fail"
    out_lines = [
        "=== Seed Release Bundle Verify Report ===",
        f"status={status}",
        f"root={root}",
        f"expected_seed_tag={expected_seed_tag}",
        *rows,
    ]
    for err in errors:
        out_lines.append(f"FAIL|{err}")
    out_lines.append("")
    report_path.write_text("\n".join(out_lines), encoding="utf-8")
    print("\n".join(out_lines))
    return 0 if status == "pass" else 2


if __name__ == "__main__":
    raise SystemExit(main())
