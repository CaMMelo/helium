# Helium Testing Guide

This document describes the Helium test harness, test file formats, and how to
add new tests.

## Running tests

From the repository root:

```sh
./tests/run_tests.py              # run all tests
./tests/run_tests.py --verbose    # show stdout/stderr for every test
./tests/run_tests.py --list       # list discovered tests
./tests/run_tests.py tests/lexer  # run only lexer tests
./tests/run_tests.py tests/parser/good/simple_function.hel
```

`make test` also invokes the harness.

In the future `hel test` will invoke `tests/run_tests.py` directly and propagate
its exit status.

## Directory layout

Tests are organized by the compiler phase or language construct they exercise.
Each phase has `good/` and `bad/` subdirectories:

```
tests/
├── run_tests.py
├── lexer/
│   ├── good/
│   └── bad/
├── parser/
│   ├── good/
│   └── bad/
├── type/
│   ├── good/
│   └── bad/
├── codegen/
│   ├── good/
│   └── bad/
├── modules/
│   ├── good/
│   └── bad/
├── ffi/
│   ├── good/
│   └── bad/
├── stdlib/
│   ├── good/
│   └── bad/
└── pm/
    ├── good/
    └── bad/
```

- `good/` — valid programs that must compile (and run, when applicable).
- `bad/` — invalid programs that must fail at the expected phase with a
  recognizable error.

## Test file formats

### Helium source tests (`.hel`)

A `.hel` test is a Helium program.  The harness compiles it with `helium` and,
for good cases, runs the resulting binary.

Metadata lives in leading line comments that start with `// @`:

```helium
// @name simple_function
// @phase parser
// @expect success

import std.io

add = (a: i32, b: i32): i32 { a + b };

main = () : IO<()> {
    io.println(f"add(1, 2) = {add(1, 2)}");
}
```

Supported metadata keys:

| Key | Meaning | Default |
|-----|---------|---------|
| `@name` | Short test name | basename without `.hel` |
| `@phase` | Phase/category | inferred from directory |
| `@expect` | `success` or `error` | inferred from `good/` or `bad/` |
| `@skip` | Optional reason to skip the test | — |
| `@match` | Regex that must match stdout (good) or stderr (bad) | sidecar `.err` file |
| `@output` | Exact expected stdout | sidecar `.out` file |

### Sidecar files

- `<name>.out` — exact expected standard output for a good test.
- `<name>.err` — regex pattern that must appear in standard error for a bad
  test.

Sidecar files are preferred over inline `@output`/`@match` for multi-line or
complex expectations.

### Generic command tests (`.test`)

Package-manager and integration tests that are not Helium programs can use
`.test` description files.  Comments start with `# @`:

```
# @name hel_init
# @phase pm
# @expect success
# @command build/bin/hel init mypackage
# @match Created project
```

The harness runs `@command` with the repository root as the working directory
and checks the exit status and output/error patterns.

## Test expectations

### Good cases

- Compile with `helium <file.hel> -o <binary>` must succeed.
- If an `.out` file or `@output` is present, the program's stdout must match
  exactly.
- If a `@match` pattern is present, stdout must match the regex.

### Bad cases

- Compile must fail (non-zero exit status).
- The `@phase` metadata records the intended compiler phase (`lex`, `parse`,
  `type`, `codegen`, `modules`, `ffi`, `stdlib`, `pm`).
- stderr must match the `.err` file or `@match` regex.

## Skipped tests

While the bootstrap compiler and package manager are still placeholders, tests
that require a real compiler are reported as `SKIP`.  Skipped tests do not count
as failures.  Once the relevant tool is implemented, the same tests will run and
produce `PASS` or `FAIL`.

## Adding a new test

1. Choose the phase directory (`tests/<phase>/`).
2. Place good tests under `good/` and bad tests under `bad/`.
3. Write a small, self-contained `.hel` program or `.test` description.
4. Add metadata and, when needed, `.out` or `.err` sidecar files.
5. Run the harness to confirm the test is discovered and behaves as expected.

Keep tests small and focused on a single construct or rule.  Follow the Helium
formatting conventions in `docs/lang/examples.md`.
