# SPEC-007: Modules, Imports, and FFI

## Goal

Implement module resolution, imports, separate compilation, and foreign function
interface.

## Dependencies

- SPEC-002 Parser and AST
- SPEC-003 Type System
- SPEC-006 LLVM Backend

## Deliverables

- `src/libhelium/modules.h/.c` — module resolution and interface files.
- `src/libhelium/ffi.h/.c` — FFI declaration handling.
- Interface file format `.hei`.
- Tests in `tests/modules/` and `tests/ffi/`.

## Requirements

1. Resolve imports against:
   - The source file's own directory (for relative imports).
   - Explicit module search paths passed via `-I` / `opts.module_paths`.
   - Cached dependencies under `.helium/<name>/<version>/`.
   The compiler does not walk ancestor directories or implicitly add `cwd/lib`.
2. Load module interface files (`.hei`) for type checking.
3. Emit `.hei` files when compiling a module.
4. Support `module Name;` declarations.
5. Support qualified access `module.name`.
6. Allow `foreign name : Type;` declarations, including generic foreign
   declarations of the form `foreign name<T> : Type;`.
7. Generate correct calling-convention glue for foreign functions.
8. Link foreign libraries specified in `Heliumfile`. Package C archives
   produced by `hel` (SPEC-009) are a package-manager-level linking concern,
   distinct from `link =` flags in a Heliumfile.

## Interface file format

A textual format listing exported names with their types:

```
module math
pi : f64
add : fn(i32, i32) -> i32
length<T> : fn([T; 0]) -> i32
```

Record type definitions are part of the format.  They appear after the
`module` line and before the export signatures:

```
module argparse
type Required = { ok: bool, value: str, error: str }
type Box<T> = { value: T }
require_option : fn(str) -> Required
```

Grammar: `type IDENT [< IDENT (, IDENT)* >] = { IDENT : TYPE (, IDENT : TYPE)* }`,
where TYPE is the same type grammar used in export signatures.  Field order is
declaration order and is preserved across the round trip, so provider and
consumer agree on the record layout.  ADT definitions are not part of the
format, and a module does not re-export record definitions it imported itself.

## Acceptance criteria

- [x] A program imports a local `lib/` module (installed into `.helium/` by
      `hel build`) and uses its values.
- [x] A program imports a cached dependency module and links to its compiled
      artifacts.
- [x] A foreign function declared in Helium can call a C function from a linked
      library.
- [x] Missing modules and name resolution failures produce clear errors.
- [x] End-to-end codegen tests for modules and FFI produce runnable binaries.
- [x] A module with its own imports (e.g. a lib module importing `std.string`)
      compiles to `.o`/`.hei`; the emitted `.hei` exports only the module's own
      names; dependents link and run.
- [x] Record type definitions are emitted to and loaded from `.hei` files; a
      consumer can access fields of a record returned by an imported function
      without redeclaring the type.
