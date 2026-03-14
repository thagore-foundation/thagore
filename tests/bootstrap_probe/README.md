# Bootstrap Probe

This minimal project exercises the features that must stay solid before
bootstrap is declared ready:

- `std.fs`: file writing, file reading, existence checks, path joining, file sizing
- `std.string`: concatenation for deterministic probe payloads
- `std.time`: runtime clock availability without relying on integer formatting

Steps:

1. `thagc build tests/bootstrap_probe/main.tg -o probe`
2. `thagc run tests/bootstrap_probe/main.tg`
3. Ensure `tests/bootstrap_probe/probe-result.txt` contains the written probe payload

The CI bootstrap workflow will:

- build the Thagore toolchain (`thagc`)
- use it to compile this probe
- run the probe
- rebuild the probe with the produced binary (double build)
