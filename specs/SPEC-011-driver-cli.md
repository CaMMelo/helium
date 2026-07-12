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
   helium <file.hel>         # compile to executable
   helium --emit-ast ...     # print AST
   helium --emit-ir ...      # print monomorphic IR
   helium --emit-llvm ...    # print LLVM IR
   helium -o <name> ...      # output name
   helium -I <path> ...      # add module search path
   helium -L <path> ...      # add library search path
   helium -l <lib> ...       # link library
   ```
2. The driver orchestrates lexer, parser, type checker, monomorphizer, and code
   generator.
3. It reports errors with file, line, and column.
4. It exits with a non-zero code on failure.
5. It integrates with the runtime object and links the final executable.

## Acceptance criteria

- [ ] `helium src/main.hel` produces an executable.
- [ ] `--emit-ast`, `--emit-ir`, and `--emit-llvm` produce readable output.
- [ ] Errors are located and actionable.
- [ ] The package manager can invoke the driver without issues.
