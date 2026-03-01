# Thagore Language Reference v1.5

This reference defines the stable language surface for Thagore v1.5.

## Program entry

- top-level executable statements are allowed, or
- explicit `func main()` entrypoint.
- mixing top-level executable statements with `func main()` is rejected.

## Declarations

- `func`, `async func`
- `extern func`
- `struct`, `impl`
- `enum` (with payload)
- `type`
- `trait`, `impl Trait for Type`
- `state Set: Variant | Variant`
- `import`, `from ... import ...`

## Statements

- `let`
- assignment
- `if` / `else`
- `while`, `for ... in ...`
- `match`
- `return`
- `defer`
- `break`, `continue` (including labels)

## Types

- primitives: `i32`, `i64`, `f32`, `f64`, `bool`, `string`, `ptr`, `void`
- collections: tuple, array
- sum helpers: `Option`, `Result`
- memory model: `Rc`, `Arc`
- function values / closures
- state-annotated type slots: `Session[Ready]`

## Concurrency and memory model

- structured concurrency APIs with scope/cancel/timeout contracts.
- compile-time send/sync checks with actionable diagnostics.
- `Rc` for single-thread ownership sharing.
- `Arc` for cross-thread sharing.

## IO and deploy surface

- HTTP/WS/DB runtime adapters.
- single-binary compiler distribution.
- one-command target initialization and cross-target build flow.

## Diagnostics contract

Every compiler diagnostic includes:

- code
- message
- file, line, column
- fix suggestion

## Stability statement

v1.5 keeps source compatibility for this documented surface unless explicitly called out in release notes.
