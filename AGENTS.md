# Agent Rules for the Helium Project

Read this file before modifying code, tests, or documentation in this repository.

## 1. Language identity

- The language is named **Helium**.
- Source files use the extension **`.hel`**.
- The package manager is named **`hel`**.
- The dependency cache directory is **`.helium/`**.
- The manifest is **`Heliumfile`** and the lock file is **`Heliumfile.lock`**.

## 2. No compiler magic

- The compiler must not contain mock builtins whose only purpose is to make a
  demo work.
- Avoid builtins. Every builtin must have a documented, unavoidable reason to
  exist. At the moment the only known unavoidable builtin is **`IO<T>`**, because
  it represents the runtime bridge to effectful computation.
- Operations such as `io.println`, `io.prints`, numeric conversions, etc. must
  be implemented in the standard library and reachable through the documented
  FFI. String concatenation is not provided as a library operation; use
  f-strings, which are a language-level feature.
- If you need a new builtin, write a spec change and justify why it cannot be
  a library/FFI feature.

## 3. Bootstrap stack

The bootstrap compiler is built with:

- C (C11 or later)
- flex
- bison
- LLVM (via the LLVM-C API)

Do not introduce new dependencies into the bootstrap compiler without
documenting them in the relevant spec.

## 4. Testing discipline

Every language construct and, where applicable, every BNF rule must have
 tests for:

- **Good cases:** valid programs that exercise normal behavior.
- **Bad cases:** invalid programs that the compiler must reject with a clear
  error.

Tests live under `tests/` and are organized by construct. Do not rely on a
few representative examples; exercise each rule directly. Prefer small,
self-contained programs over large integration tests when possible.

## 5. Minimal changes

Make the smallest change that satisfies the spec. Avoid refactoring unrelated
code. Avoid adding features that are not requested by the current task.

## 6. Specs are the source of truth

- `docs/lang/` describes what the language is.
- `specs/` describes what each implementation task must deliver.
- When you change behavior, update the corresponding spec and doc files.
- If a spec is unclear, ask for clarification rather than guessing.

## 7. Code style

- C code: use the Linux kernel coding style.
- Helium code: examples in docs and tests should follow the formatting shown in
  `docs/lang/examples.md`.
- Keep line lengths reasonable (<= 100 characters when possible).

## 8. Agent coordination

- Each agent focuses on a single spec from `specs/`.
- If your task depends on another spec, read its deliverables; do not reimplement
  them.
- Update the roadmap and spec status when you complete or block a task.
- Delegated work follows the persona contracts in `agents/`:
  - `agents/tech-lead.md` — read-only architecture and technology advice.
  - `agents/senior-developer.md` — task scoping and pre-merge code review.
  - `agents/staff-developer.md` — implementation of one scoped task at a time.
  Staff deliveries are reviewed by the senior developer before a separate
  integration step merges them; the project manager coordinates dispatch,
  parallelism, and merge order.

## 9. FFI rules

- FFI declarations are explicit and typed.
- A foreign function looks like an ordinary Helium function from the caller's
  point of view.
- The compiler emits the correct calling-convention glue; it does not special-case
  standard library calls.

## 10. Error messages

Compiler errors must be:

- Located (file, line, column when available).
- Concise.
- Actionable.

Add error-message tests to bad-case test suites.
