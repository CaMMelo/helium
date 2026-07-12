# SPEC-002: Parser and AST

## Goal

Implement the Helium parser using bison and define the abstract syntax tree.

## Dependencies

- SPEC-001 Lexer

## Deliverables

- `src/libhelium/parser.y` — bison grammar.
- `src/libhelium/ast.h` and `src/libhelium/ast.c` — AST node types and constructors.
- `src/libhelium/ast_printer.h/.c` — optional pretty printer for debugging.
- Tests in `tests/parser/` with good and bad programs.

## Requirements

The parser must handle the full grammar described in `docs/lang/`:

1. **Modules** — optional `module Name;` declaration, `import` statements.
2. **Top-level declarations** — type definitions and value bindings.
3. **Type definitions** — records and ADTs with optional type parameters.
4. **Bindings** — `name = expr;` and `name: Type = expr;`.
5. **Expressions**:
   - literals, identifiers, function calls
   - binary/unary operators with C precedence
   - blocks
   - `if`/`else`
   - `match`
   - `loop`/`recur`
   - function literals
   - record literals
   - array literals
   - type annotations `expr: Type`
   - monadic bind `expr >>= expr`
6. **Foreign declarations** — `foreign name : Type;`.

## AST design

Nodes should cover at minimum:

- `Module`, `Import`
- `TypeDef` (record, ADT)
- `Binding`
- `Foreign`
- `Literal` (int, float, bool, string, unit)
- `Identifier`
- `Binary`, `Unary`
- `Call`
- `Block`
- `If`
- `Match`, `MatchArm`, `Pattern`
- `Loop`, `Recur`
- `Lambda` (function literal)
- `RecordLiteral`, `ArrayLiteral`
- `Type`, `TypeParam`
- `Bind` (monadic `>>=`)

## Acceptance criteria

- [ ] Every example program in `docs/lang/` and `examples/` parses successfully.
- [ ] For **every BNF rule** in the grammar there is at least one good-case
      test showing a valid instance of that rule and at least one bad-case test
      showing an invalid instance. Examples of bad cases include, but are not
      limited to: missing `else`, unbalanced braces, malformed type
      definitions, malformed record literals, malformed match arms, and
      misplaced operators.
- [ ] Every bad-case test produces a syntax error with file, line, and column
      information.
- [ ] The AST can be consumed by the type checker in SPEC-003.
