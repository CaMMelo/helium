# `hel` Package Manager Specification

## 1. Overview

`hel` is the command-line package manager for Helium. It is a separate tool from
the compiler. It manages manifests, dependencies, builds, and execution.

## 2. Commands

### `hel init [name]`

Creates a new Helium project with the following layout:

```
.
├── build/
├── lib/
│   └── math.hel
├── src/
│   └── main.hel
├── tests/
│   └── smoke_test.hel
├── .helium/
├── .env
├── Heliumfile.lock
└── Heliumfile
```

- `build/` — build artifacts.
- `lib/` — local library modules.
- `src/` — application source.
- `tests/` — test sources.
- `.helium/` — dependency cache, organized as `.helium/<name>/<version>/`.
- `.env` — environment variables for local development.
- `Heliumfile` — project manifest.
- `Heliumfile.lock` — resolved dependency graph.

Default files created by `init`:

- `src/main.hel` containing a minimal `main` that imports `std.io` and prints a greeting.
- `lib/math.hel` containing a minimal module skeleton.
- `tests/smoke_test.hel` containing a minimal test entry point.

`hel init` does not copy the test harness into the project.  `hel test` invokes the repository's `tests/run_tests.py` directly, passing the project directory with `--project`.

### `hel build`

Resolves dependencies, compiles the project, and produces an executable in
`build/`.

Steps:

1. Read `Heliumfile` and `Heliumfile.lock`.
2. Ensure cached dependencies exist in `.helium/`.
3. Invoke the Helium compiler with module search paths for:
   - the project root,
   - the project's `lib/` directory,
   - the repository's `lib/` directory (so `import std.io` works),
   - each versioned directory under the project's `.helium/` cache.
4. Link the resulting objects and any required `.so`/`.o` dependencies.
5. Place the final binary in `build/`.

`hel build` does not create symlinks of `Heliumfile`, `Heliumfile.lock`, or `.helium` inside `src/`.

### `hel run`

Builds the project if needed and then runs the produced binary.

### `hel test`

Builds the project if needed and then runs the test suite defined in
`tests/`.  It invokes the repository's `tests/run_tests.py` with `--project
<project-root>` so the harness discovers and runs the project's own tests.
`hel test` reports pass/fail per test and exits with a non-zero status if any
 test fails.

### `hel add <package>[@<version>]`

Adds a dependency to `Heliumfile`, resolves the dependency graph, downloads
compiled artifacts and interface definitions into `.helium/`, and updates
`Heliumfile.lock`.

### `hel remove <package>`

Removes a dependency from `Heliumfile` and updates the lock file.

### `hel update [package]`

Updates dependencies to the latest compatible version and regenerates the lock
file. If a package is specified, only that package is updated.

## 3. Manifest format (`Heliumfile`)

TOML-based:

```toml
[package]
name = "myproject"
version = "0.1.0"
edition = "2025"

[dependencies]
std = { version = "0.1.0", registry = "https://packages.helium.dev" }
```

## 4. Dependency layout

Dependencies are stored as compiled artifacts plus interface metadata:

```
.helium/
└── std/
    └── 0.1.0/
        ├── libstd.so
        ├── libstd.o
        └── interface/
            └── io.hei
```

The compiler uses the interface files for type checking and the object/shared
files for linking. In the bootstrap implementation the cache must also contain
a matching `.hel` source stub so the compiler can resolve the import path; the
stub is not recompiled when the object and interface files are up to date.

## 5. Registries

A registry serves packages as tarballs containing compiled artifacts and
metadata. The default registry is configured in `Heliumfile` or a global config
file.

## 6. Lock file

`Heliumfile.lock` records the exact versions and checksums of resolved
dependencies so that builds are reproducible.

## 7. Good and bad cases

The package manager must have tests for:

- Creating a project with `init`.
- Building a project with no dependencies.
- Building a project with local `lib/` modules.
- Running tests with `test`.
- Adding, removing, and updating dependencies.
- Failing when a dependency cannot be found.
- Failing when the lock file is inconsistent with the manifest.
