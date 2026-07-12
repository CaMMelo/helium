# Helium Language Overview

This document is the high-level specification of Helium. It is intended as the
first reference for anyone implementing or using the language.

## 1. Philosophy

Helium is a practical, purely functional systems language. It keeps the
discipline of functional programming (immutability, explicit effects,
referential transparency) while using familiar C-style syntax and compiling to
fast native code.

Key design principles:

- **Clarity over cleverness.** Constructs should be easy to read and reason
  about.
- **No hidden magic.** The compiler translates the language; it does not
  silently invent semantics.
- **Types are a tool, not a tax.** Type annotations are optional where inference
  works, but the type system is always statically enforced.
- **Effects are explicit.** All side effects live in the `IO` monad.

## 2. Core features

| Feature | Status |
|--------|--------|
| Pure functions | Required |
| Single assignment | Required |
| Strong static typing with inference | Required |
| C-style syntax | Required |
| First-class functions | Required |
| Closures | Required |
| Tail-call optimization | Required |
| Algebraic data types | Required |
| Records | Required |
| Arrays | Required |
| String interpolation | Required |
| Pattern matching | Required |
| Generics | Required |
| Monads and `>>=` | Required |
| Reference counting | Required |
| Modules and imports | Required |
| FFI | Required from the start |
| Separate package manager | Required |

## 3. Non-goals

- Object-oriented inheritance.
- Mutable variables or mutable fields.
- Garbage collection (reference counting is deterministic).
- Exceptions or exceptions-based control flow.
- Implicit effects (all effects must be in `IO`).
- Compiler builtins that are not absolutely necessary.

## 4. Execution model

A Helium program is a collection of modules. One module exports a value
`main : IO<()>`. The runtime initializes the reference-counting runtime, calls
`main`, and exits with the returned code.

## 5. Compilation pipeline

```
.hel source
    -> lexer (flex)
    -> parser (bison)
    -> AST
    -> type checker / inference
    -> monomorphization
    -> intermediate representation
    -> LLVM IR
    -> object / executable
```

## 6. Package model

Programs are organized by the `hel` package manager. Dependencies are shipped
as compiled `.o`/`.so` artifacts plus their module interface definitions and are
cached under `.helium/<name>/<version>/`.

## 7. Relationship to specs

- `syntax.md` — lexical and concrete syntax.
- `types.md` — type system and inference.
- `expressions.md` — expressions, blocks, conditionals, loops, match.
- `functions.md` — functions, closures, recursion.
- `modules.md` — modules, imports, visibility, FFI.
- `monads.md` — monads, `IO`, `>>=`.
- `memory.md` — reference counting and ownership.
