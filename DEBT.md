# DEBT.md

- [ ] [lexer] Add dedicated TokenKind for `and`, `or`, `not`, `in` — currently handled as contextual keywords in parser
      Introduced: d79c3120
      Fix: Add dedicated lexer tokens, emit them from the lexer, and remove parser-side contextual handling for these operators and keywords.

- [ ] [lexer] Export `InternedStr(u32)` publicly
      Introduced: d79c3120
      Fix: Expose the canonical interned string handle from the lexer crate so downstream crates can use a single symbol type.

- [ ] [parser] Remove internal symbol table, use lexer interner instead once lexer exports InternedStr
      Introduced: d79c3120
      Fix: Delete the parser-local symbol table and thread lexer-owned interned symbols directly into AST construction.

- [ ] [typeck] Remove explicit symbol-name registration once the frontend exposes a canonical symbol resolver
      Introduced: 8b684c66
      Fix: Thread the lexer or parser interner through type checking so builtins, arrays, and nominal types can be resolved without a manual symbol-name side table.

- [ ] [ast] Add a struct literal AST node so type checking can validate field-complete struct construction
      Introduced: 8b684c66
      Fix: Extend the AST and parser with a dedicated struct literal expression node and then add the missing type rule in `typeck`.

- [ ] [ir] Replace synthetic derived symbols for flow compensation and method lowering with interner-backed names
      Introduced: 58e54dab
      Fix: Thread a canonical interner through IR lowering so synthesized helper functions and lowered method calls can preserve stable, collision-free symbolic names.

- [ ] [ir] Lower top-level `let` declarations into a module initializer instead of rejecting them
      Introduced: 58e54dab
      Fix: Add a synthetic module initialization function or global data lowering path so top-level bindings become executable IR instead of an `InvalidLoweringState` error.

- [ ] [codegen] Upgrade the LLVM backend from the local LLVM 14 toolchain to the requested LLVM 17 surface
      Introduced: uncommitted
      Fix: Move the build environment and `inkwell` feature gate to `llvm17-0`, then revalidate object emission, optimization passes, and integration tests against LLVM 17.

- [ ] [codegen] Replace the current debug-info façade with real DWARF emission through LLVM DIBuilder
      Introduced: uncommitted
      Fix: Thread source files and span-to-line mapping into codegen, create a compile unit plus subprogram metadata, and attach instruction locations when `--debug` is enabled.

- [ ] [codegen] Preserve `intent` metadata through IR and emit it as LLVM metadata nodes
      Introduced: uncommitted
      Fix: Extend IR to carry intent annotations, then lower them into named LLVM metadata such as `!thagore.intent` instead of dropping them before backend emission.
