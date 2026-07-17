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
5. Provide a `helium_main_wrapper` that receives `argc`/`argv` from the C
   runtime, builds a Helium array of strings when `main` expects arguments,
   invokes `main`, and exits with a status code.
6. The runtime exports reference counting (`helium_retain`, `helium_release`)
   and the allocation helpers above, `helium_array_length`,
   `helium_array_get_str`, `helium_main_wrapper`, `io_unit` (needed by the
   `hel init` scaffold and import-free codegen tests), `string_length`, and
   `list_length`. The io entry points (`io_println`, `io_prints`,
   `io_print_int`, `io_print_bool`, `io_read_line`) move to the `libs/std`
   package's csrc (SPEC-008). The dead `helium_format_*` helpers are deleted:
   f-strings lower to `snprintf` in codegen.

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
