# Helium

Helium is a purely functional, compiled programming language with C-style syntax,
strong static typing with inference, single assignment, and reference-counted
memory management.

- **Extension:** `.hel`
- **Package manager:** `hel`
- **Manifest:** `Heliumfile`
- **Lock file:** `Heliumfile.lock`
- **Dependency cache:** `.helium/`

## Status

This repository is in the bootstrap phase. The front end (lexer, parser, type
checker), monomorphizer, package manager, test harness, and LLVM IR generation
are implemented. End-to-end code generation is currently blocked by a runtime
linking bug in the compiler driver.

## Design goals

1. Purely functional — no mutable variables or side effects outside `IO`.
2. Compiled to native code through LLVM.
3. Single assignment — every name is bound exactly once in its scope.
4. Strong, static type system with global type inference.
5. Types are optional to write but always enforced by the compiler.
6. Memory management through deterministic reference counting.
7. C-based syntax for expressions, blocks, and comments.
8. First-class functions with implicit return of the last expression.
9. Full monad support; only `IO` is a builtin monad/type, operations like
   `io.println` live in the standard library and are reached through FFI.
10. No compiler magic or mock builtins; the language must be implementable from
    the standard library and documented FFI.

## Bootstrap

The bootstrap compiler is written in C, using:

- flex for lexical analysis
- bison for parsing
- LLVM-C API for code generation

See `docs/lang/` for the language specification and `specs/` for the
implementation tasks.

## Bootstrap build

```bash
make        # build compiler, package manager, and runtime
make test   # run phase-specific harnesses and the general harness
make clean  # remove build artifacts
```

## Quick start

```bash
hel init myproject
cd myproject
hel build       # builds the project
hel run         # builds if needed and runs the binary
hel test        # runs the test harness on the project
```

End-to-end execution of compiled programs is currently blocked by a known
linking bug in the compiler driver; `hel build` and `hel run` succeed up to the
link step.

## Example

```helium
import std.io

main = () : IO<()> {
    io.println("Hello World!");
}
```

## Repository layout

```
.
├── AGENTS.md           # Rules for contributors / agents
├── README.md           # This file
├── docs/               # Language and package manager specs
├── specs/              # Implementation specs for agents
├── src/                # Bootstrap compiler and package manager
├── tests/              # Test suites
└── examples/           # Example Helium programs
```

## License

To be defined.
