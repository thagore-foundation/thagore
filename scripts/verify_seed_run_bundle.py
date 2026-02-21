import argparse
import json
import subprocess
import sys
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
    args = parser.parse_args()

    run_root = Path(args.root)
    expected_commit = args.expected_commit_sha.strip()
    report_path = ROOT / args.report

    rows: list[str] = []
    errors: list[str] = []

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
        prov_commit = str(prov_meta.get("commit_sha", "")).strip()

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

    status = "pass" if not errors else "fail"
    out_lines = [
        "=== Seed Run Bundle Verify Report ===",
        f"status={status}",
        f"root={run_root}",
        f"expected_commit_sha={expected_commit}",
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
