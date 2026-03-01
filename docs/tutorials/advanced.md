# Advanced Tutorial (v1.5)

## Goal

Apply structured concurrency, typestate checks, and LSP/diagnostic workflows.

## Steps

1. Define state contracts:
   - `state Session: Init | Ready | Closed`
2. Use `thagc state explain` and `thagc state doctor`.
3. Add concurrent tasks with scope/cancel/timeout runtime contracts.
4. Validate memory model boundaries (`Rc` vs `Arc`).
5. Integrate editor support:
   - `thagc lsp --stdio`

## Outcome

You can run production-grade checks for correctness and developer UX together.
