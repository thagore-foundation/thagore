# Iterative RUP Plan

## Inception
- Freeze baseline branch.
- Define domain vocabulary and parity goals.

## Elaboration
- Establish Clean Architecture boundaries.
- Implement ports/adapters and dependency rules.
- Build CI parity extraction and deterministic checks.

## Construction
- Incrementally replace legacy behavior with C++ implementations.
- Expand test pyramid: unit -> integration -> end-to-end parity.

## Transition
- Release `thagc` stable artifacts.
- Run soak + deterministic gates across Linux/macOS/Windows.

