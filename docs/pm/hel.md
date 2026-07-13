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

- `src/main.hel` containing a minimal `main` that returns a no-op `IO<()>` action without importing any external module.
- `lib/math.hel` containing a minimal module skeleton with a tiny exported
  `add` function.
- `tests/smoke_test.hel` containing a minimal runnable codegen test that returns a no-op `IO<()>` action without importing any external module.

`hel init` does not copy the test harness into the project.  `hel test` invokes the repository's `tests/run_tests.py` directly, passing the project directory with `--project`.

### `hel build`

Resolves dependencies, compiles local libraries, and produces an executable in
`build/`.

Steps:

1. Read `Heliumfile` and `Heliumfile.lock`.
2. Ensure cached dependencies exist in `.helium/`.
3. Recursively scan the project's `lib/` directory for `.hel` source files.
   For each `lib/<pkg>/<module>.hel` (or `lib/<module>.hel` at the top level):
   - compile it to `build/lib/<pkg>/<module>.o` and emit
     `build/lib/<pkg>/<module>.hei`;
   - copy the `.hel`, `.hei`, and `.o` files into
     `.helium/<pkg>/<version>/`, using the project version from `Heliumfile`.
   Local libraries are therefore made available to `src/main.hel` through the
   `.helium/` cache, not through the raw `lib/` directory.
4. Invoke the Helium compiler on `src/main.hel`.  The compiler is given only
   the explicit `.helium/<name>/<version>/` cache directories as module search
   paths; it does not walk ancestor directories or implicitly add `lib/`.
5. Link the resulting objects and any required `.so`/`.o` dependencies.
6. Place the final binary in `build/`.

`hel build` does not create symlinks of `Heliumfile`, `Heliumfile.lock`, or `.helium` inside `src/`.

### `hel run`

Builds the project if needed and then runs the produced binary.
Extra arguments after `run` are forwarded to the binary:

```
hel run arg1 arg2
```

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

Local libraries follow the same cache layout: a project with `lib/std/io.hel`
and version `0.1.0` installs the compiled module to
`.helium/std/0.1.0/io.{hel,hei,o}`.  Imports in `src/main.hel` still use the
original package name (`import std.io`), which the compiler resolves against
the `.helium/std/0.1.0/` cache directory.

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
