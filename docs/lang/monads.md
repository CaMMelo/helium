# Helium Monads

## 1. Monad concept

A monad in Helium is a type constructor `M<T>` together with two operations:

- `return : T -> M<T>` (also called `pure`)
- `bind : M<T> -> (T -> M<U>) -> M<U>` (written `>>=`)

Helium supports user-defined monads through type classes or an equivalent
mechanism, but the first version focuses on the `IO` monad and the explicit
`>>=` operator.

## 2. `IO` is builtin

`IO<T>` is the only known unavoidable builtin type. It represents a
computation that performs side effects and produces a value of type `T`.
`IO<()>` is the type of an effectful computation that returns unit.

`IO` is builtin because it is the bridge between the pure language and the
runtime; without it, no effectful program could be expressed. Any other
builtin must be justified in the same way.

## 3. Bind operator

`>>=` sequences two monadic computations:

```helium
main = () : IO<()> {
    io.println("Hello") >>= (_: ()) {
        io.println("World")
    }
};
```

The right-hand side is a function that receives the result of the left-hand
side and returns a new monadic value. `>>=` is left-associative.

## 4. No compiler magic for monads

The compiler knows `IO` and `>>=` as ordinary builtin types and operators.
`io.println`, `io.prints`, and other IO operations are defined in the standard
library using FFI. They are not builtin.

## 5. Return / pure

A function or value that lifts a pure value into `IO` may be provided by the
standard library:

```helium
io.pure = <T>(x: T): IO<T> { ... };
```

Whether `return`/`pure` is builtin or library-defined will be specified in the
code generation spec.

## 6. Other monads

Once type classes or a trait system is added, user-defined monads such as
`Option`, `Result`, and `List` will also support `>>=` through the same
interface.
