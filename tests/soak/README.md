# Soak Tests

Soak suites execute repeated build+run loops to catch flakiness in compile/link/runtime lanes.

Current suite:

- `tests/soak/test_compiler_stress.py`: repeated compiler build+run iterations.
