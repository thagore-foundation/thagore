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
