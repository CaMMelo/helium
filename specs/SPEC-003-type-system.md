# SPEC-003: Type System and Inference

## Goal

Implement type checking and inference for Helium.

## Dependencies

- SPEC-002 Parser and AST

## Deliverables

- `src/libhelium/types.h/.c` — type representation and unification.
- `src/libhelium/inference.h/.c` — type inference engine.
- `src/libhelium/type_env.h/.c` — type environment and module interfaces.
- Tests in `tests/type/` covering good and bad cases.

## Requirements

1. Represent all primitive and compound types from `docs/lang/types.md`.
2. Implement type variables and substitutions.
3. Implement unification with occurs check.
4. Implement let-polymorphism: generalize types for top-level and let bindings.
5. Infer types for:
   - literals
   - identifiers and polymorphic calls
   - function applications
   - operators
   - blocks
   - conditionals
   - pattern matching
   - loops and `recur`
   - function literals
   - record and array literals
6. Enforce explicit type annotations when present.
7. Special handling:
   - `main` must have type `IO<()>`.
   - ADT constructors must be checked against their declared payloads.
   - Generic functions must be instantiated correctly at call sites.

## Acceptance criteria

- [x] All valid programs from the language docs type-check.
- [x] Mismatched types, missing variants, wrong arity, and escaping type
      variables are rejected with clear errors.
- [x] `loop`/`recur` argument types match the loop bindings.
- [x] `if` branches unify to a common type.
- [x] Results are consumable by SPEC-004 (monomorphization).

## Notes

- `IO<T>` is treated as the only builtin type so that `main : IO<()>` can be
  enforced; the runtime/IO implementation is provided by SPEC-005.
- The type checker is exercised by the dedicated `tests/type/run_tests.sh`
  harness, which is invoked from `make test`. The general `tests/run_tests.py`
  harness skips the `type/` directory because it is covered by the dedicated
  suite.
