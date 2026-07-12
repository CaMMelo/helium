# SPEC-009: Package Manager

## Goal

Implement the `hel` command-line package manager.

## Dependencies

- SPEC-006 LLVM Backend (uses compiler)
- SPEC-007 Modules and FFI (uses dependency cache)

## Deliverables

- `src/hel/main.c` or equivalent — `hel` CLI.
- Manifest parser for `Heliumfile`.
- Lock file generator/updater for `Heliumfile.lock`.
- Tests in `tests/pm/`.

## Requirements

1. `hel init [name]` creates the project structure defined in
   `docs/pm/hel.md`.
2. `hel build` reads the manifest, resolves dependencies, and invokes the
   compiler.
3. `hel run` builds and runs the binary.
4. `hel test` builds the project if needed, discovers tests, and runs them.
   It reports pass/fail per test and exits non-zero on failure.
5. `hel add <pkg>[@<version>]` adds a dependency and updates the lock file.
5. `hel remove <pkg>` removes a dependency and updates the lock file.
6. `hel update [pkg]` updates dependencies and the lock file.
7. Dependencies are cached under `.helium/<name>/<version>/` as compiled
   artifacts plus `.hei` interface files.
8. Provide offline fallbacks and clear errors for network failures.

## Acceptance criteria

- [ ] `hel init` produces the expected directory tree and default files.
- [ ] `hel build` compiles a project with no dependencies.
- [ ] `hel run` executes the compiled program.
- [ ] `hel test` discovers and runs tests, reporting pass/fail.
- [ ] Adding, removing, and updating dependencies updates `Heliumfile` and
      `Heliumfile.lock` correctly.
- [ ] Error cases (missing dependency, lock mismatch) are reported clearly.
