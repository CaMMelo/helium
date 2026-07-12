# SPEC-004: Monomorphization

## Goal

Convert the typed, polymorphic AST into a monomorphic intermediate form ready
for code generation.

## Dependencies

- SPEC-003 Type System and Inference

## Deliverables

- `src/libhelium/mono.h/.c` — monomorphization pass.
- `src/libhelium/ir.h/.c` — monomorphic intermediate representation.
- Tests in `tests/mono/`.

## Requirements

1. Collect all instantiations of generic types and generic functions.
2. Generate a specialized version for each distinct type argument.
3. Replace type parameters with concrete types in the specialized bodies.
4. Ensure recursive generic functions terminate.  The monomorphizer must
   track the specialization currently being generated and reuse it for
   self-recursive calls, rather than generating a fresh specialization for
   each recursive call site.
5. Produce a list of monomorphic functions and types plus a main entry point.

## IR design

The IR should be lower-level than the AST but still target-independent:

- `Function` with parameters, return type, and body.
- `Block`, `Let`, `Return`
- `Call`, `TailCall`
- `If`, `Match`
- `Loop`, `Recur`
- `RecordAlloc`, `RecordGet`, `RecordSet` (only for construction)
- `ArrayAlloc`, `ArrayGet`
- `Literal`, `Identifier`
- `ForeignCall`
- `Retain`, `Release` (reference-counting hints)

## Acceptance criteria

- [ ] Generic programs from the docs produce only concrete functions and types.
- [ ] No type variables remain in the IR.
- [ ] The IR can be printed for debugging.
- [ ] The IR is consumable by SPEC-006 (LLVM backend).
