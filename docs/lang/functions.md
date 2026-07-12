# Helium Functions

## 1. Functions are ordinary values

Functions are declared like any other value. There is no special function
keyword.

```helium
add = (a: i32, b: i32): i32 {
    a + b
};
```

The type annotation on parameters and return type may be omitted where
inference can determine them.

## 2. Implicit return

The last expression of a function body is the return value. The `return`
keyword is optional and, when present, must behave the same way.

```helium
answer = () { 42 };

answer2 = () {
    return 42;
};
```

## 3. First-class and higher-order functions

Functions can be passed as arguments, returned from other functions, and stored
in data structures.

```helium
apply = (f: fn(i32) -> i32, x: i32): i32 {
    f(x)
};

make_adder = (x: i32): fn(i32) -> i32 {
    (y: i32): i32 {
        x + y
    }
};
```

## 4. Closures

Functions may capture variables from their enclosing scope. Captured variables
are reference-counted and kept alive as long as the closure exists.

## 5. Recursion

Recursive functions are allowed.

```helium
fib = (n: i32): i32 {
    match n {
        0 => 0,
        1 => 1,
        _ => fib(n - 1) + fib(n - 2)
    }
};
```

## 6. Tail calls

Tail calls must be optimized so that recursive loops do not grow the stack. The
`loop`/`recur` construct is sugar over tail recursion, but ordinary tail calls
should also be optimized when feasible.

## 7. Generic functions

Functions may declare type parameters.

```helium
identity = <T>(x: T): T { x };
```

Generic functions are monomorphized before code generation.

## 8. Function type syntax

```helium
fn(i32, i32) -> i32
fn() -> ()
fn(T) -> T
```

## 9. Main

The program entry point is a value named `main` with type `IO<()>`.

```helium
main = () : IO<()> {
    io.println("Hello");
}
```

## 10. Invalid function usage

The compiler must reject:

- Wrong arity in a function call.
- Argument types that do not unify with parameter types.
- Return type that does not unify with the declared return type.
