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
- [ ] Recursive functions do not overflow the stack when tail-recursive.
- [ ] Closures capture and use environment correctly.
- [ ] Reference counting frees unused values.
- [x] The compiler produces LLVM IR from a `.hel` file (`--emit-llvm`).
- [x] The compiler prints a monomorphic IR representation (`--emit-ir`).
- [x] The compiler produces a native executable from a `.hel` file.

## Known issues

None.
