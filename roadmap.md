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

## Phase 1 — Front end

| Task | Spec | Dependencies | Deliverable |
|------|------|--------------|-------------|
| Lexer | SPEC-001 | — | flex lexer + token tests |
| Parser and AST | SPEC-002 | SPEC-001 | bison parser + AST tests |
| Type system and inference | SPEC-003 | SPEC-002 | type checker tests |

Exit criteria for Phase 1:
- Every language construct can be lexed, parsed, and type-checked.
- Good and bad type cases are covered by tests.

## Phase 2 — Core compilation

| Task | Spec | Dependencies | Deliverable |
|------|------|--------------|-------------|
| Monomorphization and IR | SPEC-004 | SPEC-003 | monomorphic IR + tests |
| Reference-counting runtime | SPEC-005 | — | C runtime + memory tests |
| LLVM backend | SPEC-006 | SPEC-004, SPEC-005 | code generator + runnable binaries |
| Compiler driver | SPEC-011 | SPEC-001..006 | `helium` executable |

Exit criteria for Phase 2:
- A standalone Helium program compiles to a native executable.
- Arithmetic, recursion, closures, and conditionals work.
- Memory is reference-counted and freed.

## Phase 3 — Modules, FFI, and ecosystem

| Task | Spec | Dependencies | Deliverable |
|------|------|--------------|-------------|
| Modules, imports, FFI | SPEC-007 | SPEC-003, SPEC-006, SPEC-011 | module system + `.hei` files |
| Standard library bootstrap | SPEC-008 | SPEC-006, SPEC-007 | `std.io` via FFI |
| Package manager | SPEC-009 | SPEC-006, SPEC-007 | `hel` CLI |
| Testing framework | SPEC-010 | SPEC-006, SPEC-009 | test harness + full coverage |

Exit criteria for Phase 3:
- Projects can import local modules and dependencies from `.helium/`.
- `io.println` is provided by the standard library through FFI.
- `hel init`, `hel build`, `hel run`, and `hel test` work end to end.
- Every language construct has good and bad tests.

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

- Foundation: in review.
- SPEC-005 reference-counting runtime: implemented in
  `src/runtime/helium_runtime.h/.c` with unit tests under `tests/runtime/`.
  Pending integration with SPEC-006.
- Front end, core compilation, modules, package manager, and testing: ready to
  be assigned once this roadmap and the specs are approved.
- SPEC-010: test harness, directory layout, and representative tests are
  implemented; tests currently skip because the compiler is a placeholder.
