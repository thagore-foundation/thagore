# Bootstrap Probe

This minimal project exercises the features that must stay solid before
bootstrap is declared ready:

- `std.fs`: directory scanning, existence checks, path joining, file writing
- `std.string`: concatenation, formatting via `from_int`
- `std.time`: reproducible timestamp stamping
- `std.process`: not invoked directly but the probe is sized to be part of a larger tool

Steps:

1. `thagc build tests/bootstrap_probe/main.tg -o probe`
2. `thagc run tests/bootstrap_probe/main.tg`
3. Ensure `tests/bootstrap_probe/probe-result.txt` contains the directory scan

The CI bootstrap workflow will:

- build the Thagore toolchain (`thagc`)
- use it to compile this probe
- run the probe
- rebuild the probe with the produced binary (double build)
