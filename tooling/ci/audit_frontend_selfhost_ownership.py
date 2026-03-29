#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
ALLOWED_LEGACY = {
    pathlib.Path('tests/bootstrap_seed/README.md'),
    pathlib.Path('tests/selfhost_frontend/README.md'),
}
SEARCH_ROOTS = [
    pathlib.Path('.github/workflows'),
    pathlib.Path('bootstrap'),
    pathlib.Path('tooling'),
    pathlib.Path('tools'),
]
FORBIDDEN_SNIPPETS = [
    'tests/bootstrap_seed/',
    'tests/selfhost_frontend/',
]
ALLOWED_MANIFEST_ATOMS = {
    '.',
    'exe',
    'library',
    'core-exe',
    'core-library',
    'dump-report',
    'dump-desugared',
    'bogus-mode',
    'weird',
    'ok',
    'missing import',
    'unknown imported symbol',
    'unknown identifier',
    'call arity mismatch',
    'type mismatch',
    'condition type mismatch',
    'return type mismatch',
}
ALLOWED_REFERENCE_FILES = {
    pathlib.Path('tests/bootstrap_seed/README.md'),
    pathlib.Path('tests/selfhost_frontend/README.md'),
    pathlib.Path('docs/plan/bootstrap-readiness.md'),
    pathlib.Path('docs/plan/selfhost-bootstrap.md'),
    pathlib.Path('.github/workflows/frontend-selfhost-ownership.yml'),
    pathlib.Path('tooling/ci/audit_frontend_selfhost_ownership.py'),
}


def fail(message: str) -> None:
    raise SystemExit(message)


def assert_legacy_dirs_are_docs_only() -> None:
    for rel in [pathlib.Path('tests/bootstrap_seed'), pathlib.Path('tests/selfhost_frontend')]:
        root = REPO_ROOT / rel
        if not root.exists():
            fail(f'missing legacy audit root: {rel.as_posix()}')
        files = sorted(path.relative_to(REPO_ROOT) for path in root.rglob('*') if path.is_file())
        extra = [path for path in files if path not in ALLOWED_LEGACY]
        if extra:
            rendered = ', '.join(path.as_posix() for path in extra)
            fail(f'legacy frontend test area is not docs-only: {rendered}')


def assert_no_legacy_references() -> None:
    offenders: list[str] = []
    for search_root in SEARCH_ROOTS:
        root = REPO_ROOT / search_root
        if not root.exists():
            continue
        for path in root.rglob('*'):
            if not path.is_file():
                continue
            rel = path.relative_to(REPO_ROOT)
            if rel in ALLOWED_REFERENCE_FILES:
                continue
            if path.suffix not in {'.py', '.rs', '.tg', '.md', '.yml', '.yaml', '.txt'}:
                continue
            text = path.read_text(encoding='utf-8')
            for needle in FORBIDDEN_SNIPPETS:
                if needle in text:
                    offenders.append(f'{rel.as_posix()}: {needle}')
    if offenders:
        fail(
            'legacy frontend references remain outside allowed docs:\n'
            + '\n'.join(offenders)
        )


def assert_manifests_stay_under_bootstrap_selfhost() -> None:
    manifest_root = REPO_ROOT / 'bootstrap/selfhost/corpus'
    for path in manifest_root.glob('*.txt'):
        for raw in path.read_text(encoding='utf-8').splitlines():
            line = raw.strip()
            if not line or line.startswith('#'):
                continue
            for part in [segment.strip() for segment in line.split('|')]:
                if not part:
                    continue
                if part.startswith('@abs:'):
                    part = part[len('@abs:'):]
                if part.startswith('bootstrap/selfhost/'):
                    continue
                if part in ALLOWED_MANIFEST_ATOMS:
                    continue
                if part.isdigit():
                    continue
                # Non-path labels and relative leaf names are allowed here; the
                # ownership invariant is about path-bearing entries escaping
                # bootstrap/selfhost/.
                if '/' not in part and '\\' not in part:
                    continue
                fail(f'manifest entry escapes bootstrap/selfhost ownership: {path.relative_to(REPO_ROOT).as_posix()} -> {part}')


if __name__ == '__main__':
    assert_legacy_dirs_are_docs_only()
    assert_no_legacy_references()
    assert_manifests_stay_under_bootstrap_selfhost()
    print('frontend selfhost ownership audit ok')
