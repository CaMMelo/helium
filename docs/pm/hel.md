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
4. Compile package C sources. For each `lib/<pkg>/csrc/` directory, every
   `*.c` file found recursively (in sorted order) is compiled with
   `cc -std=c11 -O2 -g -Wall -Wextra -Ilib/<pkg>/csrc -c <file> -o build/lib/<pkg>/csrc/<relative-path>.o`.
   If a package produced at least one object, the objects are archived with
   `ar rcs build/lib/<pkg>/lib<pkg>.a <objects...>` and the archive is
   installed to `.helium/<pkg>/<version>/lib<pkg>.a` next to the installed
   modules. A package with csrc but no `.hel` modules is still built and
   installed.
5. Invoke the Helium compiler on `src/main.hel`.  The compiler is given only
   the explicit `.helium/<name>/<version>/` cache directories as module search
   paths; it does not walk ancestor directories or implicitly add `lib/`.
6. Link the resulting objects together with the full path of every
   `lib<pkg>.a` archive found in the cached version directories
   (`.helium/<pkg>/<version>/`, sorted); `hel` passes them through the
   compiler's `extra_libs` mechanism, so local packages and cached
   dependencies are linked uniformly and transitively.
7. Place the final binary in `build/`.

If `src/main.hel` is absent, `hel build` runs in library mode: it still
compiles local modules and package C sources (steps 3–4); if at least one
module or archive was installed it prints `Built libraries` and exits 0; if
nothing was produced it keeps the existing `error: src/main.hel not found`.

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

Dependencies are stored as compiled artifacts plus interface metadata, flat
inside each version directory:

```
.helium/
└── std/
    └── 0.1.0/
        ├── io.hel
        ├── io.hei
        ├── io.o
        ├── string.hel
        ├── string.hei
        ├── string.o
        ├── list.hel
        ├── list.hei
        ├── list.o
        └── libstd.a
```

Each module contributes a `.hel` source stub, a `.hei` interface file, and a
`.o` object file; `lib<pkg>.a` is present when the package ships C sources
under `csrc/`. The compiler uses the interface files for type checking and
the object files and archives for linking. The `.hel` stub is required so the
compiler can resolve the import path; the stub is not recompiled when the
object and interface files are up to date.

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
