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

- [ ] A Helium program with arithmetic and conditionals compiles and runs.
      Currently blocked by a runtime linking bug: `helium file.hel -o bin` fails
      while invoking the system C compiler to compile the runtime.
- [ ] Recursive functions do not overflow the stack when tail-recursive.
      Pending the linking fix.
- [ ] Closures capture and use environment correctly. Pending the linking fix.
- [ ] Reference counting frees unused values. Pending the linking fix.
- [x] The compiler produces LLVM IR from a `.hel` file (`--emit-llvm`).
- [x] The compiler prints a monomorphic IR representation (`--emit-ir`).
- [ ] The compiler produces a native executable from a `.hel` file. Pending the
      linking fix.

## Known issues

- The final link step in `src/libhelium/compiler.c` passes a corrupted runtime
  object path to the system C compiler, causing linking to fail with a message
  such as `linker input file unused because linking not done`. This prevents
  all end-to-end codegen tests from producing executables. The AST, IR, and
  LLVM IR emission paths are unaffected.
