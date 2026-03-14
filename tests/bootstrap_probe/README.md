# Bootstrap Probe

This project now exercises a broader bootstrap slice instead of a single-file
smoke test.

Surface covered:

- local module imports via `from . import ...`
- `std.string`: split, join, concat, len, integer formatting, equality
- `std.fs`: write, read, file sizing
- `std.path`: cwd, path join, directory detection
- `std.process`: environment lookup and argv access
- `std.time`: monotonic time sanity checks
- arrays and `for` traversal through `string.split(...)`

Steps:

1. `thagc build tests/bootstrap_probe/main.tg -o probe`
2. Set `THAGORE_BOOTSTRAP_PROBE=ok`
3. `thagc run tests/bootstrap_probe/main.tg`
4. Ensure `tests/bootstrap_probe/probe-result.txt` matches `tests/bootstrap_probe/expected.txt`

The CI bootstrap workflow will:

- build the Thagore toolchain (`thagc`)
- run interpreter parity audits
- run native stdlib audits for `string`, `time`, `fs`, `path`, and `process`
- compile the probe twice with the built toolchain
- run both probe binaries
- compare both outputs against `tests/bootstrap_probe/expected.txt`
