# SPEC-005: Reference-Counting Runtime

## Goal

Implement the small C runtime that supports reference counting and foreign
function integration.

## Dependencies

- None directly, but design must match SPEC-006.

## Deliverables

- `src/runtime/helium_runtime.h/.c`
- Build rules to compile `helium_runtime.o`.
- Tests in `tests/runtime/`.

## Requirements

1. Define object headers for heap-allocated values (arrays, records, ADTs,
   strings, closures).
2. Implement `helium_retain` and `helium_release`.
3. Implement destructors for runtime object kinds.
4. Provide allocation helpers:
   - `helium_alloc_string`
   - `helium_alloc_array`
   - `helium_alloc_record`
   - `helium_alloc_adt`
   - `helium_alloc_closure`
5. Provide a `helium_main_wrapper` that invokes `main` and exits with a status
   code.
6. Provide helpers for string interpolation if they cannot be generated inline
   (to be decided with SPEC-006).

## Acceptance criteria

- [ ] Reference counts are incremented and decremented correctly in generated
      programs.
- [ ] Memory is freed when counts reach zero.
- [ ] No leaks in simple test programs.
- [ ] Runtime links cleanly with LLVM-generated object files.
