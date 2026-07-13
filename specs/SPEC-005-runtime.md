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
6. Provide C entry points for the standard-library IO operations that are
   declared as `foreign` in `lib/std/io.hel` (e.g. `io_println`, `io_prints`,
   `io_printi`). String interpolation is generated inline by the backend.

## Acceptance criteria

- [x] Runtime unit tests (`tests/runtime/run_tests.sh`) pass, including under
      valgrind when available.
- [x] Reference counts are incremented and decremented correctly in generated
      programs. The codegen now emits `helium_release` for owned heap values at
      scope exit and for discarded temporaries, and keeps returned values alive
      via ownership transfer.
- [x] Memory is freed when counts reach zero. Verified by runtime unit tests and
      valgrind on generated programs that allocate records.
- [x] No leaks in simple test programs. `codegen/hello` and
      `codegen/record_alloc` run cleanly under valgrind.
- [x] Runtime links cleanly with LLVM-generated object files. End-to-end codegen
      tests compile, link, and run successfully.
- [x] `tests/runtime/run_tests.sh` is integrated into `make test`.
