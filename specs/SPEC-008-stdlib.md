# SPEC-008: Standard Library Bootstrap

**Status:** complete

## Goal

Create the minimal standard library needed to write useful Helium programs,
without compiler magic. The standard library lives in the `libs/std` package;
its effectful functions are implemented in package C sources and linked by
`hel` (SPEC-009).

## Dependencies

- SPEC-006 LLVM Backend
- SPEC-007 Modules and FFI
- SPEC-009 Package Manager (csrc compilation and archive linking)

## Deliverables

- `libs/std/Heliumfile`
- `libs/std/lib/std/io.hel`
- `libs/std/lib/std/string.hel`
- `libs/std/lib/std/list.hel`
- `libs/std/lib/std/csrc/io.c`
- `libs/std/lib/std/csrc/string.c`
- Tests in `tests/stdlib/`.

## Requirements

1. `std.io` must provide:
   - `println : str -> IO<()>`
   - `prints : str -> IO<()>` (prints without newline)
   - `print_int : i32 -> IO<()>`
   - `print_bool : bool -> IO<()>`
   - `read_line : () -> IO<str>` (reads a line from stdin)
2. The io functions are implemented in the package's `csrc/` C sources
   (`libs/std/lib/std/csrc/io.c`) and compiled, archived, installed, and
   linked by `hel` per SPEC-009; they are declared `foreign` in
   `libs/std/lib/std/io.hel`. Only `io_unit` remains in the runtime.
3. No special compiler support for `io.println`; it is an ordinary imported
   function.
4. `IO<T>` values are sequenced with the `>>=` operator.  Generic `pure` and
   `bind` operations are not yet available in this bootstrap because the
   interface format does not preserve type variables across modules and the
   type system distinguishes `T` from `IO<T>`.
5. `std.string` must provide:
   - `length : str -> i32`
   - `is_empty : str -> bool`
   - `equals : fn(str, str) -> bool` — content equality via `strcmp`,
     implemented in `libs/std/lib/std/csrc/string.c`. Justified because the
     language has no string content comparison; `==` on `str` is pointer
     comparison.
6. `std.list` documents that arrays are the list-like aggregate in this
   bootstrap.  Generic `map`/`filter`/`fold` are deferred because they require
   dynamic allocation or element access not yet exposed to library code through
   the FFI.  `length` is provided as a generic foreign function that works for
   arrays of any element type.

## Acceptance criteria

- [x] `import std.io; main = () : IO<()> { io.println("Hello"); }` compiles in
      a project that depends on the `libs/std` package and prints "Hello".
- [x] Chained IO actions with `>>=` work using standard library functions.
- [x] `std.string` functions, including `equals`, work via FFI to the
      package's csrc.
- [x] `std.list` provides a working array length demonstration.
- [x] The standard library builds as the `libs/std` package and is installed
      and linked by `hel` per the SPEC-009 csrc convention.
- [x] Good and bad case tests exist under `tests/stdlib/`.

## Notes

- `str` is represented as a C string pointer in the bootstrap runtime, so
  `std.string.length` is implemented with `strlen` and `std.string.equals`
  with `strcmp`.
- Arrays are represented as `helium_array_t` pointers; `std.list.length` reads
  the runtime `length` field.  It is declared as a generic foreign function
  `length<T> : fn([T; 0]) -> i32`, so it can be used with arrays of any
  element type.
- A codegen bug where binary/unary operators used lexer token codes instead of
  bison token codes was fixed so that `std.string.is_empty` and other
  comparisons produce correct LLVM IR.
