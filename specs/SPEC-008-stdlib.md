# SPEC-008: Standard Library Bootstrap

## Goal

Create the minimal standard library needed to write useful Helium programs,
without compiler magic.

## Dependencies

- SPEC-006 LLVM Backend
- SPEC-007 Modules and FFI

## Deliverables

- `lib/std/io.hel`
- `lib/std/string.hel` (optional in first version)
- `lib/std/list.hel` (optional in first version)
- Tests in `tests/stdlib/`.

## Requirements

1. `std.io` must provide:
   - `println : str -> IO<()>`
   - `prints : str -> IO<()>` (prints without newline)
2. These functions must be implemented via FFI to a C runtime function (e.g.,
   `puts` or a custom runtime helper).
3. No special compiler support for `io.println`; it is an ordinary imported
   function.
4. Provide a way to construct `IO<T>` values from pure values, e.g.,
   `io.pure : T -> IO<T>`.
5. Demonstrate monadic bind with standard library functions.

## Acceptance criteria

- [ ] `import std.io; main = () : IO<()> { io.println("Hello"); }` compiles and
      prints "Hello". The `lib/std/io.hel` module and runtime entry points are
      implemented; execution is blocked by the SPEC-006 linking issue.
- [ ] Chained IO actions with `>>=` work. Pending the SPEC-006 linking fix.
- [x] Standard library modules are loadable as ordinary imported modules by the
      compiler and package manager.
