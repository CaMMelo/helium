# SPEC-011: Compiler Driver

## Goal

Implement the `helium` compiler executable used by `hel` and developers.

## Dependencies

- SPEC-001 Lexer
- SPEC-002 Parser
- SPEC-003 Type System
- SPEC-004 Monomorphization
- SPEC-006 LLVM Backend
- SPEC-007 Modules and FFI

## Deliverables

- `src/helium/main.c` — compiler driver.
- Command-line interface.
- Tests in `tests/driver/`.

## Requirements

1. Command-line interface:
   ```
   helium <file.hel>              # compile to executable
   helium <file.hel> -o <name>    # specify output name
   helium --emit-ast <file.hel>   # print AST
   helium --emit-ir <file.hel>    # print monomorphic IR
   helium --emit-llvm <file.hel>  # print LLVM IR
   helium -I <path> ...           # add module search path
   helium -L <path> ...           # add library search path
   helium -l <lib> ...            # link library
   helium -v / --version          # print version
   ```
   When no output name is given, the executable name is the input file with
   the `.hel` extension removed.
2. The driver orchestrates lexer, parser, type checker, monomorphizer, and code
   generator.
3. It reports errors with file, line, and column.
4. It exits with a non-zero code on failure.
5. It integrates with the runtime object and links the final executable.

## Acceptance criteria

- [x] `helium src/main.hel` produces an executable.
- [x] `--emit-ast`, `--emit-ir`, and `--emit-llvm` produce readable output.
- [x] Errors are located and actionable.
- [x] The package manager can invoke the driver without issues.
- [x] The driver exits with a non-zero status on failure.
- [x] The driver produces a runnable executable from a `.hel` file.
