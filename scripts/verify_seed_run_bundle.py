import argparse
import json
import struct
import subprocess
import sys
import tarfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_TAGS = [
    "linux-x86_64",
    "linux-arm64",
    "macos-arm64",
    "windows-x86_64",
    "windows-arm64",
]
ASSET_MAP = {
    "linux-x86_64": (
        "thagore-stage1-linux-x86_64.tar.gz",
        "thagore-runtime-linux-x86_64.a",
    ),
    "linux-arm64": (
        "thagore-stage1-linux-arm64.tar.gz",
        "thagore-runtime-linux-arm64.a",
    ),
    "macos-arm64": (
        "thagore-stage1-macos-arm64.tar.gz",
        "thagore-runtime-macos-arm64.a",
    ),
    "windows-x86_64": (
        "thagore-stage1-windows-x86_64.exe",
        "thagore-runtime-windows-x86_64.lib",
    ),
    "windows-arm64": (
        "thagore-stage1-windows-arm64.exe",
        "thagore-runtime-windows-arm64.lib",
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


def _find_exact(root: Path, name: str) -> list[Path]:
    return sorted(p for p in root.rglob(name) if p.is_file())


def _find_single(root: Path, name: str) -> Path:
    hits = _find_exact(root, name)
    if len(hits) != 1:
        raise ValueError(f"expected exactly one '{name}', found={len(hits)}")
    return hits[0]


def _parse_manifest(path: Path) -> dict[str, str]:
    data: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        value = value.strip()
        if key and key not in data:
            data[key] = value
    return data


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
        if len(blob) < 8:
            return set()
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
        description="Verify atomic seed bundles/provenance from a Seed Stage1 Assets workflow run."
    )
    parser.add_argument("--root", required=True, help="Path to downloaded run artifacts root.")
    parser.add_argument(
        "--expected-commit-sha",
        required=True,
        help="Expected commit SHA from workflow_run.head_sha.",
    )
    parser.add_argument("--report", default="seed-run-bundle-verify.txt")
    parser.add_argument("--expected-run-id", default="")
    parser.add_argument("--expected-run-attempt", default="")
    args = parser.parse_args()

    run_root = Path(args.root)
    expected_commit = args.expected_commit_sha.strip()
    expected_run_id = args.expected_run_id.strip()
    expected_run_attempt = args.expected_run_attempt.strip()
    report_path = ROOT / args.report

    rows: list[str] = []
    errors: list[str] = []
    seed_tags_seen: set[str] = set()
    repositories_seen: set[str] = set()

    if not run_root.exists():
        errors.append(f"missing artifacts root: {run_root}")
    if len(expected_commit) < 7:
        errors.append("expected commit SHA is missing/invalid")

    for asset_tag in DEFAULT_TAGS:
        stage1_name, runtime_name = ASSET_MAP[asset_tag]
        try:
            bundle_path = _find_single(run_root, f"seed-bundle-{asset_tag}.json")
            manifest_path = _find_single(run_root, f"seed-promotion-manifest-{asset_tag}.txt")
            prov_path = _find_single(run_root, f"seed-stage1-provenance-{asset_tag}.json")
            stage1_path = _find_single(run_root, stage1_name)
            runtime_path = _find_single(run_root, runtime_name)
        except Exception as exc:
            errors.append(f"{asset_tag}: {exc}")
            continue

        rows.append(f"OK|located|{asset_tag}|bundle={bundle_path}")
        rows.append(f"OK|located|{asset_tag}|manifest={manifest_path}")
        rows.append(f"OK|located|{asset_tag}|provenance={prov_path}")
        rows.append(f"OK|located|{asset_tag}|stage1={stage1_path.name}")
        rows.append(f"OK|located|{asset_tag}|runtime={runtime_path.name}")
        ok_arch, arch_msg = _verify_stage1_arch(asset_tag, stage1_path)
        if not ok_arch:
            errors.append(arch_msg)
            continue
        rows.append(f"OK|stage1_arch|{arch_msg}")

        manifest_data = _parse_manifest(manifest_path)
        schema = manifest_data.get("schema", "")
        manifest_asset_tag = manifest_data.get("asset_tag", "")
        manifest_arch = manifest_data.get("arch", "")
        manifest_commit = manifest_data.get("commit_sha", "")
        manifest_seed_tag = manifest_data.get("seed_tag", "")
        manifest_run_id = manifest_data.get("run_id", "")
        manifest_run_attempt = manifest_data.get("run_attempt", "")
        if schema != "thagore.seed.promote.v1":
            errors.append(f"{asset_tag}: manifest schema mismatch: {schema}")
            continue
        if manifest_asset_tag != asset_tag:
            errors.append(
                f"{asset_tag}: manifest asset_tag mismatch expected={asset_tag} got={manifest_asset_tag}"
            )
            continue
        if manifest_commit != expected_commit:
            errors.append(
                f"{asset_tag}: manifest commit mismatch expected={expected_commit} got={manifest_commit}"
            )
            continue
        if expected_run_id and manifest_run_id != expected_run_id:
            errors.append(
                f"{asset_tag}: manifest run_id mismatch expected={expected_run_id} got={manifest_run_id}"
            )
            continue
        if expected_run_attempt and manifest_run_attempt != expected_run_attempt:
            errors.append(
                f"{asset_tag}: manifest run_attempt mismatch expected={expected_run_attempt} got={manifest_run_attempt}"
            )
            continue
        rows.append(f"OK|manifest_consistency|{asset_tag}|arch={manifest_arch}")
        if manifest_seed_tag:
            seed_tags_seen.add(manifest_seed_tag)

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
        bundle_id = str(bundle_meta.get("bundle_id", "")).strip().lower()
        bundle_seed_tag = str(bundle_meta.get("seed_tag", "")).strip()
        bundle_asset_tag = str(bundle_meta.get("asset_tag", "")).strip()
        bundle_arch = str(bundle_meta.get("arch", "")).strip()
        bundle_run_id = str(bundle_meta.get("run_id", "")).strip()
        bundle_run_attempt = str(bundle_meta.get("run_attempt", "")).strip()
        bundle_repository = str(bundle_meta.get("repository", "")).strip()
        prov_commit = str(prov_meta.get("commit_sha", "")).strip()
        prov_seed_tag = str(prov_meta.get("seed_tag", "")).strip()
        prov_asset_tag = str(prov_meta.get("asset_tag", "")).strip()
        prov_arch = str(prov_meta.get("arch", "")).strip()
        prov_run_id = str(prov_meta.get("run_id", "")).strip()
        prov_run_attempt = str(prov_meta.get("run_attempt", "")).strip()
        prov_repository = str(prov_meta.get("repository", "")).strip()

        if bundle_commit != expected_commit:
            errors.append(
                f"{asset_tag}: bundle commit mismatch expected={expected_commit} got={bundle_commit}"
            )
            continue
        if prov_commit != expected_commit:
            errors.append(
                f"{asset_tag}: provenance commit mismatch expected={expected_commit} got={prov_commit}"
            )
            continue
        rows.append(f"OK|commit_match|{asset_tag}|{expected_commit}")
        if bundle_asset_tag != asset_tag or prov_asset_tag != asset_tag:
            errors.append(
                f"{asset_tag}: asset_tag mismatch bundle={bundle_asset_tag} provenance={prov_asset_tag}"
            )
            continue
        if bundle_arch != manifest_arch or prov_arch != manifest_arch:
            errors.append(
                f"{asset_tag}: arch mismatch manifest={manifest_arch} bundle={bundle_arch} provenance={prov_arch}"
            )
            continue
        if expected_run_id and (bundle_run_id != expected_run_id or prov_run_id != expected_run_id):
            errors.append(
                f"{asset_tag}: run_id mismatch bundle={bundle_run_id} provenance={prov_run_id} expected={expected_run_id}"
            )
            continue
        if expected_run_attempt and (
            bundle_run_attempt != expected_run_attempt or prov_run_attempt != expected_run_attempt
        ):
            errors.append(
                f"{asset_tag}: run_attempt mismatch bundle={bundle_run_attempt} provenance={prov_run_attempt} expected={expected_run_attempt}"
            )
            continue
        if manifest_seed_tag and (bundle_seed_tag != manifest_seed_tag or prov_seed_tag != manifest_seed_tag):
            errors.append(
                f"{asset_tag}: seed_tag mismatch manifest={manifest_seed_tag} bundle={bundle_seed_tag} provenance={prov_seed_tag}"
            )
            continue
        if bundle_repository:
            repositories_seen.add(bundle_repository)
        if prov_repository and bundle_repository and prov_repository != bundle_repository:
            errors.append(
                f"{asset_tag}: repository mismatch bundle={bundle_repository} provenance={prov_repository}"
            )
            continue
        rows.append(f"OK|metadata_consistency|{asset_tag}")

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
                expected_commit,
                "--expected-bundle-id",
                bundle_id,
            ]
        )
        if rc != 0:
            errors.append(f"{asset_tag}: stage1_provenance verify failed: {out}")
            continue
        rows.append(f"OK|stage1_provenance_verify|{asset_tag}")

    if len(seed_tags_seen) != 1:
        errors.append(f"seed_tag mismatch across manifests: {sorted(seed_tags_seen)}")
    else:
        rows.append(f"OK|seed_tag_consistent|{next(iter(seed_tags_seen))}")
    if len(repositories_seen) > 1:
        errors.append(f"repository mismatch across bundles: {sorted(repositories_seen)}")
    elif len(repositories_seen) == 1:
        rows.append(f"OK|repository_consistent|{next(iter(repositories_seen))}")

    status = "pass" if not errors else "fail"
    out_lines = [
        "=== Seed Run Bundle Verify Report ===",
        f"status={status}",
        f"root={run_root}",
        f"expected_commit_sha={expected_commit}",
        f"expected_run_id={expected_run_id}",
        f"expected_run_attempt={expected_run_attempt}",
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
