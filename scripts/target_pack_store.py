import argparse
import shutil
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def _store_root(path_arg: str) -> Path:
    if path_arg.strip():
        return Path(path_arg).expanduser().resolve()
    return Path.home() / ".thagc" / "targets"


def _source_root(path_arg: str) -> Path:
    if path_arg.strip():
        return Path(path_arg).expanduser().resolve()
    return ROOT / "targets" / "packs"


def cmd_install(args: argparse.Namespace) -> int:
    target = args.target.strip()
    if not target:
        raise SystemExit("ERROR: empty target")
    src_root = _source_root(args.source_root)
    src = src_root / target
    if not src.exists() or not src.is_dir():
        raise SystemExit(f"ERROR: target pack source missing: {src}")
    dst_root = _store_root(args.store_root)
    dst = dst_root / target
    dst_root.mkdir(parents=True, exist_ok=True)
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst)
    print(f"installed={target}")
    print(f"path={dst}")
    return 0


def cmd_remove(args: argparse.Namespace) -> int:
    target = args.target.strip()
    if not target:
        raise SystemExit("ERROR: empty target")
    dst = _store_root(args.store_root) / target
    if dst.exists():
        shutil.rmtree(dst)
        print(f"removed={target}")
    else:
        print(f"removed={target}|status=missing")
    return 0


def cmd_exists(args: argparse.Namespace) -> int:
    target = args.target.strip()
    dst = _store_root(args.store_root) / target
    if dst.exists() and dst.is_dir():
        print(f"ok={target}")
        return 0
    print(f"missing={target}")
    return 2


def cmd_list(args: argparse.Namespace) -> int:
    root = _store_root(args.store_root)
    if not root.exists():
        return 0
    for item in sorted(root.iterdir()):
        if item.is_dir():
            print(item.name)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Manage installed target packs in ~/.thagc/targets.")
    parser.add_argument("--store-root", default="", help="Target pack store root (default: ~/.thagc/targets)")
    sub = parser.add_subparsers(dest="command", required=True)

    p_install = sub.add_parser("install")
    p_install.add_argument("--target", required=True)
    p_install.add_argument("--source-root", default="", help="Source pack root (default: repo targets/packs)")
    p_install.set_defaults(func=cmd_install)

    p_remove = sub.add_parser("remove")
    p_remove.add_argument("--target", required=True)
    p_remove.set_defaults(func=cmd_remove)

    p_exists = sub.add_parser("exists")
    p_exists.add_argument("--target", required=True)
    p_exists.set_defaults(func=cmd_exists)

    p_list = sub.add_parser("list")
    p_list.set_defaults(func=cmd_list)

    args = parser.parse_args()
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
