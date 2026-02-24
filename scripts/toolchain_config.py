import argparse
import json
import os
import re
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
REGISTRY_PATH = ROOT / "targets" / "registry" / "targets.json"


@dataclass
class ToolchainConfig:
    channel: str = "stable"
    toolchain_version: str = ""
    default_target: str = ""
    installed_targets: list[str] | None = None
    profile: str = "standard"

    def __post_init__(self) -> None:
        if self.installed_targets is None:
            self.installed_targets = []


def _config_path(path_arg: str) -> Path:
    if path_arg:
        return Path(path_arg).expanduser().resolve()
    env_path = os.getenv("THAGC_CONFIG", "").strip()
    if env_path:
        return Path(env_path).expanduser().resolve()
    return Path.home() / ".thagc" / "config.toml"


def _load_registry() -> dict:
    if not REGISTRY_PATH.exists():
        raise RuntimeError(f"missing target registry: {REGISTRY_PATH}")
    return json.loads(REGISTRY_PATH.read_text(encoding="utf-8"))


def _supported_targets(registry: dict) -> list[str]:
    out: list[str] = []
    for row in registry.get("targets", []):
        triple = str(row.get("triple", "")).strip()
        if triple:
            out.append(triple)
    return out


def _standard_targets(registry: dict) -> list[str]:
    vals = registry.get("standard_profile", [])
    if not isinstance(vals, list):
        return []
    return [str(v).strip() for v in vals if str(v).strip()]


def _parse_list(text: str) -> list[str]:
    inner = text.strip()
    if not inner.startswith("[") or not inner.endswith("]"):
        return []
    body = inner[1:-1].strip()
    if not body:
        return []
    parts = [p.strip() for p in body.split(",")]
    out: list[str] = []
    for p in parts:
        if p.startswith('"') and p.endswith('"') and len(p) >= 2:
            p = p[1:-1]
        if p:
            out.append(p)
    return out


def read_config(path: Path) -> ToolchainConfig:
    if not path.exists():
        return ToolchainConfig()
    text = path.read_text(encoding="utf-8")

    def get_str(key: str, default: str = "") -> str:
        m = re.search(rf'^\s*{re.escape(key)}\s*=\s*"([^"]*)"\s*$', text, re.MULTILINE)
        return m.group(1).strip() if m else default

    def get_list(key: str) -> list[str]:
        m = re.search(rf'^\s*{re.escape(key)}\s*=\s*(\[[^\n]*\])\s*$', text, re.MULTILINE)
        if not m:
            return []
        return _parse_list(m.group(1))

    return ToolchainConfig(
        channel=get_str("channel", "stable"),
        toolchain_version=get_str("toolchain_version", ""),
        default_target=get_str("default_target", ""),
        installed_targets=get_list("installed_targets"),
        profile=get_str("profile", "standard"),
    )


def write_config(path: Path, cfg: ToolchainConfig) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    quoted_targets = ",".join([f'"{t}"' for t in cfg.installed_targets])
    lines = [
        f'channel = "{cfg.channel}"',
        f'toolchain_version = "{cfg.toolchain_version}"',
        f'default_target = "{cfg.default_target}"',
        f"installed_targets = [{quoted_targets}]",
        f'profile = "{cfg.profile}"',
        "",
    ]
    path.write_text("\n".join(lines), encoding="utf-8")


def _normalize_targets(raw: str) -> list[str]:
    return [item.strip() for item in raw.split(",") if item.strip()]


def cmd_list_targets(args: argparse.Namespace) -> int:
    registry = _load_registry()
    standard = set(_standard_targets(registry))
    for triple in _supported_targets(registry):
        mark = "standard" if triple in standard else "optional"
        print(f"{triple}\t{mark}")
    return 0


def cmd_installed(args: argparse.Namespace) -> int:
    cfg = read_config(_config_path(args.config))
    for triple in cfg.installed_targets:
        print(triple)
    return 0


def cmd_init(args: argparse.Namespace) -> int:
    registry = _load_registry()
    supported = set(_supported_targets(registry))
    cfg_path = _config_path(args.config)
    cfg = read_config(cfg_path)

    profile = args.profile.strip()
    if profile not in {"standard", "custom"}:
        raise SystemExit("ERROR: --profile must be standard or custom")

    if profile == "standard":
        targets = _standard_targets(registry)
    else:
        targets = _normalize_targets(args.targets)
        if not targets:
            raise SystemExit("ERROR: --targets is required for custom profile")

    for triple in targets:
        if triple not in supported:
            raise SystemExit(f"ERROR: unsupported target triple: {triple}")

    default_target = args.default_target.strip() or (targets[0] if targets else "")
    if default_target and default_target not in supported:
        raise SystemExit(f"ERROR: unsupported default target: {default_target}")

    cfg.channel = args.channel.strip() or "stable"
    cfg.toolchain_version = args.toolchain_version.strip()
    cfg.default_target = default_target
    cfg.installed_targets = targets
    cfg.profile = profile
    write_config(cfg_path, cfg)
    print(f"config={cfg_path}")
    print(f"profile={cfg.profile}")
    print(f"default_target={cfg.default_target}")
    print(f"installed_targets={','.join(cfg.installed_targets)}")
    return 0


def cmd_add(args: argparse.Namespace) -> int:
    registry = _load_registry()
    supported = set(_supported_targets(registry))
    triple = args.target.strip()
    if triple not in supported:
        raise SystemExit(f"ERROR: unsupported target triple: {triple}")
    cfg_path = _config_path(args.config)
    cfg = read_config(cfg_path)
    if triple not in cfg.installed_targets:
        cfg.installed_targets.append(triple)
    if not cfg.default_target:
        cfg.default_target = triple
    write_config(cfg_path, cfg)
    print(f"added={triple}")
    print(f"config={cfg_path}")
    return 0


def cmd_remove(args: argparse.Namespace) -> int:
    triple = args.target.strip()
    cfg_path = _config_path(args.config)
    cfg = read_config(cfg_path)
    cfg.installed_targets = [t for t in cfg.installed_targets if t != triple]
    if cfg.default_target == triple:
        cfg.default_target = cfg.installed_targets[0] if cfg.installed_targets else ""
    write_config(cfg_path, cfg)
    print(f"removed={triple}")
    print(f"config={cfg_path}")
    return 0


def cmd_ensure(args: argparse.Namespace) -> int:
    triple = args.target.strip()
    cfg = read_config(_config_path(args.config))
    if triple in cfg.installed_targets:
        print(f"ok={triple}")
        return 0
    print(f"missing={triple}")
    return 2


def main() -> int:
    parser = argparse.ArgumentParser(description="Manage thagc toolchain target config.")
    parser.add_argument("--config", default="", help="Config file path (default: ~/.thagc/config.toml)")
    sub = parser.add_subparsers(dest="command", required=True)

    p_list = sub.add_parser("list-targets", help="List all supported target triples.")
    p_list.set_defaults(func=cmd_list_targets)

    p_inst = sub.add_parser("installed", help="List installed targets from user config.")
    p_inst.set_defaults(func=cmd_installed)

    p_init = sub.add_parser("init", help="Initialize toolchain config.")
    p_init.add_argument("--profile", required=True, choices=["standard", "custom"])
    p_init.add_argument("--targets", default="", help="Comma-separated targets for custom profile")
    p_init.add_argument("--default-target", default="")
    p_init.add_argument("--toolchain-version", default="")
    p_init.add_argument("--channel", default="stable")
    p_init.set_defaults(func=cmd_init)

    p_add = sub.add_parser("add", help="Add installed target.")
    p_add.add_argument("--target", required=True)
    p_add.set_defaults(func=cmd_add)

    p_rm = sub.add_parser("remove", help="Remove installed target.")
    p_rm.add_argument("--target", required=True)
    p_rm.set_defaults(func=cmd_remove)

    p_ensure = sub.add_parser("ensure", help="Exit non-zero if target is not installed.")
    p_ensure.add_argument("--target", required=True)
    p_ensure.set_defaults(func=cmd_ensure)

    args = parser.parse_args()
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
