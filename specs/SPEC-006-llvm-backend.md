# SPEC-006: LLVM Code Generation

## Goal

Generate LLVM IR and native code from the monomorphic IR.

## Dependencies

- SPEC-004 Monomorphization
- SPEC-005 Runtime

## Deliverables

- `src/libhelium/codegen.h/.c` — LLVM-C API code generator.
- `src/libhelium/compiler.h/.c` — high-level compile function.
- Tests in `tests/codegen/` producing runnable binaries.

## Requirements

1. Translate each monomorphic function into LLVM IR.
2. Generate code for all expression forms in the IR.
3. Implement tail-call optimization where possible.
4. Emit reference-counting operations around bindings, calls, and returns.
5. Implement closures as structs containing function pointer and captured
   environment.
6. Implement records and ADTs as structs with a tag field for variants.
7. Implement arrays as a header plus contiguous elements.
8. Implement string interpolation by generating calls to runtime or standard
   library helpers.
9. Generate a `main` C function that calls the Helium `main` through the runtime
   wrapper.
10. Compile and link with the runtime object and any foreign libraries.

## Acceptance criteria

- [x] A Helium program with arithmetic and conditionals compiles and runs.
      Covered by `tests/codegen/good/generic_recursive.hel`.
- [ ] Recursive functions do not overflow the stack when tail-recursive.
      Only `recur`/`loop` tail calls are marked; ordinary function calls still
      need tail-call optimization.
- [ ] Closures capture and use environment correctly. Closure allocation is not
      yet emitted by the backend (`helium_alloc_closure` is unused).
- [x] Reference counting frees unused values. `helium_release` is emitted for
      owned heap values at scope exit and for discarded temporaries. Verified
      leak-free under valgrind for records, arrays, and variants
      (`tests/codegen/good/record_no_leak.hel`, `record_alloc.hel`, `hello.hel`).
- [x] The compiler produces LLVM IR from a `.hel` file (`--emit-llvm`).
- [x] The compiler prints a monomorphic IR representation (`--emit-ir`).
- [x] The compiler produces a native executable from a `.hel` file. End-to-end
      codegen tests compile, link, and run successfully.
