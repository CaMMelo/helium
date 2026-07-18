# SPEC-012: Argument Parser Package (argparse)

**Status:** complete

## Goal

Provide a `hel` package `libs/argparse` with purely functional queries over
the `[str]` argument array passed to `main`, so programs can ask which
command-line flags and options they received without any compiler magic. The
package performs no I/O; callers print results themselves through `std.io`
(AGENTS.md §2). The package is tested with `hel test` inside its own project.

## Dependencies

- SPEC-008 Standard Library (`std.string.equals` for content comparison)
- SPEC-009 Package Manager (the package builds and tests via `hel`)

## Deliverables

- `libs/argparse/Heliumfile` and `libs/argparse/Heliumfile.lock`.
- `libs/argparse/lib/argparse/argparse.hel` — the library module.
- Good and bad case tests in `libs/argparse/tests/`, run by `hel test`.

## Language constraints (verified against the bootstrap compiler)

- `==` on `str` is pointer comparison (`src/libhelium/codegen.c`). All content
  comparison goes through `string.equals` from `std.string`.
- ADTs and `match` do not dispatch at runtime in this bootstrap, so results
  are records, never ADTs, and the library uses no `match`.
- There is no dynamic array construction, no substring/split/prefix
  operation, no `str` to `i32` conversion, no `exit`, and no stderr.
  Therefore flags match whole tokens exactly and option values are the token
  that follows the option name.

## Requirements

1. The module is `argparse`, imported as `import argparse.argparse` and used
   as `argparse.has_flag(...)` etc. (cache-prefix stripping resolves it, the
   same mechanism as `std.io`).
2. Public API (exactly this surface):

   ```helium
   /* True when `--long` or `--short` appears as a whole token in args. */
   has_flag = (args: [str], long: str, short: str): bool { ... };

   /* Token following `--long`/`--short`; `default` when absent or value
      missing. */
   option_value = (args: [str], long: str, short: str, default: str): str { ... };

   /* Result of a required option. */
   type Required = { ok: bool, value: str, error: str };

   /* ok=true with value when present; ok=false and an f-string error naming
      the option when absent. error is "" when ok. */
   require_option = (args: [str], long: str, short: str): Required { ... };
   ```

3. Matching convention: callers pass the full token to match, e.g.
   `"--verbose"` as `long` and `"-v"` as `short`; the library prepends
   nothing. Passing `""` as `short` disables short matching (an empty token
   is never matched as a flag name).
4. Flags match whole tokens only, via `string.equals`. An option's value is
   the token immediately following the option token; if the option token is
   the last element of `args`, the value is missing.
5. Implementation walks `args` by index with `loop`/`recur` and `args[i]`
   (the idiom proven in `examples/echo.hel`). Array length comes from
   `std.list.length`.
6. The library performs no I/O and declares no `foreign` functions; it only
   imports `std.string` and `std.list`.
7. Out of scope for v1 (rejected by design, not parsed): `--name=value`
   joined form, short-flag clustering (`-abc`), and positional-argument
   queries. These need string operations the bootstrap does not have.
8. Tests live in the package's own `tests/` directory and are discovered and
   run by `hel test`: good cases exercise hit and miss paths and print via
   `std.io`; bad cases assert clear compile errors for a wrong-arity call and
   for importing a nonexistent `argparse` submodule.

## Acceptance criteria

- [x] `libs/argparse` builds with `hel build` and installs
      `.helium/argparse/0.1.0/` with the module source, interface, and object.
- [x] `has_flag` finds a present long or short flag and rejects absent ones.
- [x] `option_value` returns the following token when present, and the
      default when the option is absent or its value is missing.
- [x] `require_option` returns `ok=true` with the value when present, and
      `ok=false` with an f-string error naming the option when absent.
- [x] Passing `""` as `short` disables short matching.
- [x] `cd libs/argparse && ../../build/bin/hel test` passes: 3 good tests
      and 2 bad tests.
- [x] The library module contains no ADTs, no `match`, and no I/O.

## Notes

- Project tests are compiled by `tests/run_tests.py`; when `--project` is
  set, the harness adds `-I`/`-L`/`-l` flags for every directory under the
  project's `.helium/<pkg>/<version>/` cache so project tests can import the
  project's own packages. Without `--project` the harness is unchanged.
