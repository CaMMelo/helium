# SPEC-001: Lexer

## Goal

Implement the Helium lexer using flex.

## Dependencies

None.

## Deliverables

- `src/libhelium/lexer.l` — flex specification.
- `src/libhelium/token.h` — token enum and helper functions.
- Unit tests in `tests/lexer/` covering every token class.

## Requirements

The lexer must recognize:

1. **Whitespace and newlines** — ignored, but line/column tracking must be
   maintained for error reporting.
2. **Comments**:
   - Line comments starting with `//` to end of line.
   - Block comments starting with `/*` and ending with `*/` (non-nested in the
     first version).
3. **Identifiers**:
   - `[a-zA-Z_][a-zA-Z0-9_]*`
   - Keywords: `if`, `else`, `match`, `type`, `module`, `import`, `loop`,
     `recur`, `fn`, `return`, `foreign`.
4. **Integer literals** — optional leading `-`, decimal digits. Store as string
   for the parser to resolve type.
5. **Floating-point literals** — digits `.` digits.
6. **Boolean literals** — `true`, `false`.
7. **String literals**:
   - `"..."` with escape sequences `\n`, `\\`, `\"`.
   - `f"..."` with interpolated `{expr}` segments.
8. **Operators and delimiters**:
   - `+`, `-`, `*`, `/`, `%`
   - `==`, `!=`, `<`, `<=`, `>`, `>=`
   - `&&`, `||`, `!`
   - `=`, `:`, `;`, `,`, `.`, `->`, `=>`, `|`, `>>=`
   - `(`, `)`, `[`, `]`, `{`, `}`, `<`, `>`

## Implementation notes

- The lexer emits f-strings as a sequence of `FSTRING_START`,
  `FSTRING_PART`, the interpolated expression tokens, and `FSTRING_END`.
  This lets the parser handle the expression inside the braces.
- Floating-point literals are accepted with an optional leading `-` sign
  (e.g. `-3.14`) in addition to the `digits.digits` form, matching the
  treatment of signed integer literals.
- Line and column numbers are 1-based and attached to every token for error
  reporting.

## Acceptance criteria

- [x] `tests/lexer/tokens.hel` is tokenized correctly.
- [x] Bad input such as unterminated strings or comments produces an error with
      line/column information.
- [x] The lexer integrates with the parser built in SPEC-002.
- [x] F-string lexemes are emitted as the token sequence documented in the
      implementation notes so that the parser can consume interpolated
      expressions.
