# CLI Command Groups v1.5

Thagore v1.5 stable CLI surface includes 10 command groups:

1. `build`
2. `run`
3. `check`
4. `fmt`
5. `fix`
6. `repl`
7. `lsp`
8. `target`
9. `state`
10. `migrate`

These groups are enforced through:

- `contracts/manifest.json` (`required_cli_groups`)
- dispatch checks in parity tests
- integration tests for command behavior.
