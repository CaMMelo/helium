# Helium Roadmap

This roadmap lists the tasks required to complete the bootstrap compiler,
package manager, and supporting infrastructure. Each task maps to a spec in
`specs/` and has explicit dependencies. Review this file before assigning work
to agents.

## Phase 0 — Project foundation

- [x] Choose language name: **Helium**.
- [x] Initialize repository structure.
- [x] Write `README.md` and `AGENTS.md`.
- [x] Write language specification docs under `docs/lang/`.
- [x] Write package manager spec under `docs/pm/`.
- [x] Write per-agent implementation specs under `specs/`.
- [x] Write `roadmap.md`.
- [x] Add example programs under `examples/`.
- [x] Set up a build system (Makefile) for the C bootstrap.

## Phase 1 — Front end ✅

| Task | Spec | Dependencies | Deliverable |
|------|------|--------------|-------------|
| Lexer | SPEC-001 | — | flex lexer + token tests |
| Parser and AST | SPEC-002 | SPEC-001 | bison parser + AST tests |
| Type system and inference | SPEC-003 | SPEC-002 | type checker tests |

Exit criteria for Phase 1:
- [x] Every language construct can be lexed, parsed, and type-checked.
- [x] Good and bad type cases are covered by tests.

## Phase 2 — Core compilation ✅

| Task | Spec | Dependencies | Deliverable |
|------|------|--------------|-------------|
| Monomorphization and IR | SPEC-004 | SPEC-003 | monomorphic IR + tests |
| Reference-counting runtime | SPEC-005 | — | C runtime + memory tests |
| LLVM backend | SPEC-006 | SPEC-004, SPEC-005 | code generator + runnable binaries |
| Compiler driver | SPEC-011 | SPEC-001..006 | `helium` executable |

Exit criteria for Phase 2:
- [x] A standalone Helium program compiles to a native executable.
- [x] Arithmetic, recursion, closures, and conditionals work in a compiled binary.
- [x] Memory is reference-counted and freed in a compiled binary.
- [x] AST, IR, and LLVM IR emission work.
- [x] The runtime unit tests pass and are invoked by `make test`.
- [x] The compiler driver produces executables from `.hel` files and supports
      `--emit-ast`, `--emit-ir`, `--emit-llvm`, `-o`, `-I`, `-L`, `-l`, and `-v`.

## Phase 3 — Modules, FFI, and ecosystem ✅

| Task | Spec | Dependencies | Deliverable |
|------|------|--------------|-------------|
| Modules, imports, FFI | SPEC-007 | SPEC-003, SPEC-006, SPEC-011 | module system + `.hei` files |
| Standard library bootstrap | SPEC-008 | SPEC-006, SPEC-007 | `std.io` via FFI |
| Package manager | SPEC-009 | SPEC-006, SPEC-007 | `hel` CLI |
| Testing framework | SPEC-010 | SPEC-006, SPEC-009 | test harness + full coverage |

Exit criteria for Phase 3:
- [x] Projects can import local modules and dependencies from `.helium/`.
- [x] `io.println` is provided by the standard library through FFI.
- [x] `hel init`, `hel build`, `hel run`, and `hel test` are implemented and
      report results correctly.
- [x] `hel run` and `hel test` produce runnable binaries for projects that need
      code generation.
- [x] Every language construct has good and bad tests.
- [x] The standard library is no longer implicitly pulled from the repository's
      `lib/` directory; projects must add it explicitly under their own `lib/`
      or `.helium/` cache.

## Phase 4 — Self-hosting compiler

The self-hosted compiler is built with Helium-coded lexer and parser
generators. The generator input syntax is modeled on flex and bison, but the
output is Helium source code.

| Task | Spec | Dependencies | Deliverable |
|------|------|--------------|-------------|
| Lexer generator syntax and tool | — | Phase 3 | `hflex` reading `.hlex` files, emitting Helium lexers |
| Parser generator syntax and tool | — | Phase 3 | `hbison` reading `.hyacc` files, emitting Helium parsers |
| Generate compiler front end | — | Generators above | Helium lexer/parser for Helium |
| Port type checker to Helium | — | Phase 1 | Helium type checker |
| Port monomorphizer to Helium | — | Phase 2 | Helium monomorphizer |
| Port LLVM backend to Helium | — | Phase 2 | Helium code generator |
| Self-compile | — | All above | Compiler can compile itself |

Exit criteria for Phase 4:
- Lexer and parser generators can produce the Helium compiler's front end.
- The Helium compiler is written in Helium and can compile itself.

## Phase 5 — Web framework

| Task | Spec | Dependencies | Deliverable |
|------|------|--------------|-------------|
| HTTP server library | — | Phase 3 | `std.http` or separate package |
| Routing and request handling | — | HTTP library | web framework package |
| Example web application | — | Framework | working demo |

## Dependency graph

```
SPEC-001  ->  SPEC-002  ->  SPEC-003  ->  SPEC-004  ->  SPEC-006  ->  SPEC-011
                  |             |             ^
                  |             |             |
                  v             v             v
              SPEC-007 <------------------- SPEC-008
                  |                            |
                  v                            v
              SPEC-009 <------------------- SPEC-010

SPEC-005  ->  SPEC-006
```

## Current state

- Foundation: complete.
- SPEC-001..SPEC-003 front end: complete; unit tests pass.
- SPEC-004 monomorphization: complete; unit tests pass.
- SPEC-005 reference-counting runtime: complete. Implemented in
  `src/runtime/helium_runtime.h/.c`, unit tests pass under `tests/runtime/`,
  and the runtime test runner is invoked by `make test`.
- SPEC-006 LLVM backend: complete. End-to-end codegen tests compile, link, and
  run; arithmetic/conditionals, tail recursion, closures, and reference counting
  are all verified.
- SPEC-007 modules and FFI: complete. Local modules, cached dependencies,
  interface files, and foreign declarations work end-to-end.
- SPEC-008 standard library: complete. The standard library is the `libs/std`
  package: `std.io`, `std.string` (with `equals`), and `std.list` modules whose
  effectful functions are implemented in package C sources (csrc), built,
  installed, and linked by `hel`. The runtime keeps only `io_unit`,
  `string_length`, and `list_length` from the old std surface.
- SPEC-009 package manager: complete. Package csrc is compiled and archived
  as `lib<pkg>.a`, cached archives link into the final binary, and library
  mode builds packages without `src/main.hel`.
- SPEC-010 testing framework: complete. Phase-specific harnesses, the general
  harness, and CI all run `make test` with passing codegen tests.
- SPEC-011 compiler driver: complete. The driver emits AST/IR/LLVM, reports
  errors, and produces native executables.
- Recent additions:
  - `main` can accept command-line arguments as `[str]`.
  - The repository `lib/` directory is no longer implicitly added to `hel`
    search paths; the standard library must be added to a project manually.
  - Packages shipped with the compiler repository live under `libs/<pkg>/`
    (starting with `libs/std`) and are built and consumed like any other
    package through the `.helium/` cache.
  - `make test` builds `libs/std` via the `libs-std` target, and tests that
    import `std.*` resolve against the installed package cache.
- Next major goal: Phase 4 — self-hosting compiler.
