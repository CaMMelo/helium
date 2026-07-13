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

## Phase 2 — Core compilation (partial)

| Task | Spec | Dependencies | Deliverable |
|------|------|--------------|-------------|
| Monomorphization and IR | SPEC-004 | SPEC-003 | monomorphic IR + tests |
| Reference-counting runtime | SPEC-005 | — | C runtime + memory tests |
| LLVM backend | SPEC-006 | SPEC-004, SPEC-005 | code generator + runnable binaries |
| Compiler driver | SPEC-011 | SPEC-001..006 | `helium` executable |

Exit criteria for Phase 2:
- [ ] A standalone Helium program compiles to a native executable. Blocked by
      the SPEC-006 runtime linking bug.
- [ ] Arithmetic, recursion, closures, and conditionals work in a compiled
      binary. Pending the linking fix.
- [ ] Memory is reference-counted and freed in a compiled binary. Pending the
      linking fix.
- [x] AST, IR, and LLVM IR emission work.
- [x] The runtime unit tests pass.

## Phase 3 — Modules, FFI, and ecosystem (partial)

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
- [ ] `hel run` and `hel test` produce runnable binaries for projects that need
      code generation. Pending the SPEC-006 linking fix.
- [x] Every language construct has good and bad tests.

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
- SPEC-005 reference-counting runtime: implemented in
  `src/runtime/helium_runtime.h/.c` with passing unit tests under
  `tests/runtime/`. Runtime integration with generated executables is pending
  the SPEC-006 linking fix; the runtime test runner is not yet invoked by
  `make test`.
- SPEC-006 LLVM backend: LLVM IR generation works (`--emit-llvm`), but the
  final link step fails due to a corrupted runtime object path passed to the
  system C compiler.
- SPEC-007 modules and FFI: module resolution, interface files, and foreign
  declarations work; end-to-end binaries are blocked by SPEC-006.
- SPEC-008 standard library: `lib/std/io.hel` and runtime entry points are
  implemented; execution is blocked by SPEC-006.
- SPEC-009 package manager: `hel init/build/run/test/add/remove/update` are
  implemented; end-to-end runs are blocked by SPEC-006.
- SPEC-010 testing framework: complete. The general harness and phase-specific
  harnesses are implemented, CI runs `make test`, and all codegen tests pass.
- SPEC-011 compiler driver: CLI and emit passes work; executable generation is
  blocked by SPEC-006.
