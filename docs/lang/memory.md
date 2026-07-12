# Helium Memory Management

## 1. Strategy

Helium uses deterministic reference counting for memory management. Every heap
allocation carries a reference count. When the count reaches zero, the object
is freed immediately.

## 2. Ownership and sharing

- Values are shared by default.
- A value is kept alive as long as any binding, closure capture, or data
  structure references it.
- When a binding goes out of scope, its reference count is decremented.

## 3. Closures

Closures capture their environment by reference. Captured variables are
retained for the lifetime of the closure.

## 4. Cycles

Reference counting alone cannot collect cycles. In the first version, cycles
are not automatically broken. The language design avoids mutable references and
recursive closures that would easily create cycles; ADTs are immutable. A cycle
collection strategy may be added later.

## 5. Arrays, records, and ADTs

All compound values are heap-allocated and reference-counted. Element access
does not copy; it borrows. Pattern matching binds fields without copying unless
the type is a primitive.

## 6. FFI and foreign pointers

Foreign pointers returned through FFI are reference-counted wrappers around raw
pointers. The foreign side is responsible for freeing resources when the
wrapper's destructor runs.

## 7. Runtime support

The compiler emits reference-counting increments and decrements around:

- Bindings entering and leaving scope.
- Function calls and returns.
- Closure captures.
- Array/record/ADT construction and destruction.

A small runtime written in C provides the actual `retain`/`release`
implementation.
