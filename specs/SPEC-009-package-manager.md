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
6. `hel remove <pkg>` removes a dependency and updates the lock file.
7. `hel update [pkg]` updates dependencies and the lock file.
8. Dependencies are cached under `.helium/<name>/<version>/` as compiled
   artifacts plus `.hei` interface files.
9. Provide offline fallbacks and clear errors for network failures.

## Implementation notes

- `Heliumfile.lock` uses a TOML-like format with a `[package]` section and an
  `[[dependencies]]` table array. Each dependency records `name`, `version`, and
  a deterministic `checksum` of its interface files.
- `hel init` does not copy `tests/run_tests.py` into the project; `hel test`
  invokes the repository harness with `--project <project-root>`.
- `hel build` does not create symlinks of `Heliumfile`, `Heliumfile.lock`, or
  `.helium` inside `src/`.  Instead it compiles local `lib/` modules into
  `build/lib/` and installs them into `.helium/<pkg>/<version>/`, then invokes
  the compiler with explicit module search paths covering only the versioned
  directories under `.helium/`.  The compiler no longer walks ancestor
  directories or implicitly adds `cwd/lib`.
- In the bootstrap implementation, cached packages must also contain a `.hel`
  source stub alongside the compiled artifacts. The stub is required so the
  compiler can resolve the import path, but it is not recompiled when the
  object and interface files are already up to date.

## Acceptance criteria

- [x] `hel init` produces the expected directory tree and default files.
- [x] `hel build` compiles a project with no dependencies.
- [x] `hel run` executes the compiled program.
- [x] `hel test` discovers and runs tests, reporting pass/fail.
- [x] Adding, removing, and updating dependencies updates `Heliumfile` and
      `Heliumfile.lock` correctly.
- [x] Error cases (missing dependency, lock mismatch) are reported clearly.
- [x] `hel run` and `hel test` succeed for projects whose tests require
      end-to-end code generation.
