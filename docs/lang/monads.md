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

A function or value that lifts a pure value into `IO` is provided by the
standard library as an ordinary foreign-backed function. The compiler does not
need a special `return`/`pure` builtin:

```helium
io.pure = <T>(x: T): IO<T> { ... };
```

In the bootstrap implementation, `io.pure` can be defined as a foreign function
that wraps a pure value in the runtime's `IO` representation.

## 6. Other monads

Once type classes or a trait system is added, user-defined monads such as
`Option`, `Result`, and `List` will also support `>>=` through the same
interface.
