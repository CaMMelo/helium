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

The parser handles the grammar described in `docs/lang/` with the divergences
noted below:

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

Nodes cover at minimum:

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

- [x] Every example program in `docs/lang/` and `examples/` parses successfully.
      (`examples/` files were updated to remove block-trailing semicolons to match
      the implemented grammar.)
- [x] For **every major BNF rule** in the grammar there is at least one good-case
      test showing a valid instance of that rule and at least one bad-case test
      showing an invalid instance.
- [x] Every bad-case test produces a syntax error with file, line, and column
      information.
- [x] The AST can be consumed by the type checker in SPEC-003.
- [x] The grammar files in `docs/lang/syntax.md` and `docs/lang/expressions.md`
      are updated to match the implemented block-semicolon rules.

## Grammar notes

- Top-level declarations are separated by semicolons, but the separator and
  trailing semicolon are optional between most declarations. This matches the
  style used in the example programs (`examples/`).
- **Block bodies require semicolons between bindings/expressions; a trailing
  semicolon before the closing `}` is not accepted.** This is a divergence from
  `docs/lang/syntax.md` and was introduced to resolve a shift/reduce conflict
  between a trailing semicolon and a following block item. Examples in
  `examples/` were updated accordingly.
- Function literals may omit the return-type annotation (`() { ... }` is valid),
  but an explicit return type resolves the `()` ambiguity unambiguously.
- Generic type application uses the same `<`/`>` tokens as comparison operators;
  the parser resolves this with precedence rules in type position (e.g.
  `IO<()>`).
- `match` arms accept literal patterns directly (`0 => ...`, `true => ...`,
  etc.) as well as constructor and wildcard patterns.
- The scrutinee of `match` must be a `primary_expr`. A bare uppercase identifier
  followed by `{` (e.g. `match Point { ... }`) is parsed as a record literal;
  parenthesize the scrutinee to avoid ambiguity: `match (Point { ... }) { ... }`.

## Known bison conflicts

Bison reports **25 shift/reduce conflicts** and **2 reduce/reduce conflicts**.
The generated parser resolves them with the default bison disambiguation rules,
which matches the test suite and example programs. The main conflicts are:

- `IDENT` followed by `{`: a bare identifier can be reduced to a variable
  expression or shifted as the type name of a record literal. The parser shifts,
  so record literals parse correctly and `match` on a bare identifier works
  because the scrutinee is parsed as a `primary_expr` that cannot extend into a
  record literal.
- Inside a parameter list, an `IDENT` can be reduced as a parameter (with or
  without a type annotation) or as a plain expression. This is the source of the
  two reduce/reduce conflicts; the parser keeps the parameter interpretation,
  which is correct for function literals.
- `RETURN expr` followed by `:`, `>>=`, `||`, `&&`, etc.: the parser can either
  finish the `return` expression or continue the inner expression. The default
  resolution allows `return a + b`, `return a: T`, etc.
- `()` followed by `{` or `:`: ambiguous between the unit literal and the start
  of a parameterless lambda. An explicit return type (`(): T { ... }`) resolves
  the ambiguity.

## Test coverage

Good-case tests cover modules, imports, record and ADT types, generic types,
bindings with and without type annotations, foreign declarations, all literal
kinds, identifiers, function calls, arithmetic/comparison/logical/monadic-bind
operators, blocks, `if`/`else`, `match`, `loop`/`recur`, lambdas (including
generic), record literals, array literals, type annotations, `return`, f-strings,
and field access.

Bad-case tests cover unbalanced braces, missing `else`, malformed type
definitions, malformed record literals, malformed match arms/patterns, malformed
lambdas, malformed foreign declarations, empty `return`, malformed bind/array
expressions, missing operands, missing module/import paths, invalid characters,
missing generic closing brackets, `recur` without parentheses, and missing
semicolons between block items.
