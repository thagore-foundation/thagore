# LSP in VS Code (v1.4 MVP)

The v1.4 language server runs over stdio via:

```bash
thagc lsp --stdio
```

MVP capabilities:

- completion (`textDocument/completion`)
- go-to-definition (`textDocument/definition`)
- incremental document sync (`didOpen`, `didChange`)

## VS Code client wiring

Use a minimal Language Client extension configuration pointing to the command above.

Command:

```text
thagc
```

Arguments:

```text
lsp --stdio
```

## Expected behavior

1. Typing in `.tg` files returns keyword completions (`func`, `let`, `if`, `state`, ...).
2. Go-to-definition jumps to matching `func`/`let`/`struct`/`enum` symbol definitions in the opened document.
3. `initialize` handshake advertises:
   - `textDocumentSync`
   - `definitionProvider`
   - `completionProvider`

## Troubleshooting

1. Verify command exists:
   - `thagc lsp --stdio`
2. Check raw protocol with a framed `initialize` request.
3. Confirm the editor sends `didOpen` before requesting definitions.
