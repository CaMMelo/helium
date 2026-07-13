# SPEC-010: Testing Framework

## Goal

Define and implement how Helium programs and compiler passes are tested.

## Dependencies

- SPEC-006 LLVM Backend
- SPEC-009 Package Manager

## Deliverables

- `tests/run_tests.sh` or `tests/run_tests.py` test harness.
- Directory layout for good/bad tests per construct.
- Documentation in `docs/testing.md`.

## Requirements

1. Organize tests by language construct.  Each construct directory has
   `good/` and `bad/` subdirectories:
   ```
   tests/
   ├── run_tests.py
   ├── lexer/
   │   ├── good/
   │   └── bad/
   ├── parser/
   │   ├── good/
   │   └── bad/
   ├── type/
   │   ├── good/
   │   └── bad/
   ├── codegen/
   │   ├── good/
   │   └── bad/
   ├── modules/
   │   ├── good/
   │   └── bad/
   ├── ffi/
   │   ├── good/
   │   └── bad/
   ├── stdlib/
   │   ├── good/
   │   └── bad/
   └── pm/
       ├── good/
       └── bad/
   ```
2. Each test is a small Helium program (`.hel`) or a test description file
   (`.test`).  Good cases live under `good/`; bad cases live under `bad/`.
3. Good cases:
   - Must compile.
   - Must run and produce expected output when applicable.
4. Bad cases:
   - Must fail at the expected phase (lex, parse, type, codegen).
   - Must produce a recognizable error.
5. The harness must support:
   - Running a single test.
   - Running all tests.
   - Reporting failures with phase, file, and error output.
6. `hel test` invokes the harness and propagates its exit status.
7. Add tests for every construct and, where applicable, every BNF rule in the
   grammar, both valid and invalid. Do not rely on a few representative
   examples; each syntactic and semantic rule must be exercised directly.

## Acceptance criteria

- [x] Every language construct has at least one good and one bad test.
- [x] The harness discovers all tests, runs them, and reports PASS/FAIL/SKIP.
- [x] The harness handles compiler output that is not valid UTF-8 by replacing
      undecodable bytes instead of crashing.
- [x] `make test` invokes the phase-specific harnesses and the general harness.
- [x] CI invokes `make test` and sees all codegen tests pass.
