# Soak Tests

Soak suites execute repeated build+run loops to catch flakiness in compile/link/runtime lanes.

Current suite:

- `tests/soak/test_compiler_stress.py`: repeated compiler build+run iterations.
- `tests/soak/test_concurrency_long_running.py`: long-running scope churn lane.

Long-running GA lane:

- Run with `THAG_SOAK_LONG_SECONDS=3600` (or higher) to execute the 1h+ scope soak contract.
