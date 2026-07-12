# Helium Type System

## 1. Design

- Strong, static type system.
- Global type inference using a Hindley-Milner-style algorithm with
  let-polymorphism.
- Type annotations are optional but always enforced.
- No subtyping except through type variables and parametric polymorphism.
- Functions are ordinary types and ordinary values.

## 2. Primitive types

| Type | Description |
|------|-------------|
| `i8`, `i16`, `i32`, `i64` | Signed integers |
| `u8`, `u16`, `u32`, `u64` | Unsigned integers |
| `f32`, `f64` | Floating-point numbers |
| `bool` | Boolean |
| `str` | Immutable UTF-8 string |
| `()` | Unit |

Integer literals without annotation default to `i32`. Float literals default to
`f64`.

## 3. Compound types

### Array

```helium
[i32; 5]
```

Arrays have a fixed element type and a fixed size known at compile time.
Literal arrays must match the annotated size and element type when a type
annotation is present.

### Record

```helium
type Point = { x: f64, y: f64 };

p = Point{ x: 1.0, y: 2.0 };
```

Records are nominal product types. Field access uses `p.x`.

### Algebraic data type

```helium
type Shape =
    | Circle { center: Point, radius: f64 }
    | Rectangle { top_left: Point, bottom_right: Point }
    ;
```

ADTs are nominal sum types. Each variant is a constructor that may carry a
record payload. Pattern matching is the only way to inspect a value of an ADT.

## 4. Function type

```helium
fn(i32, i32) -> i32
```

Function types are first class. Higher-order functions and closures are
supported.

## 5. Type parameters

Types and functions may be generic:

```helium
type Option<T> =
    | None
    | Some<T>
    ;

identity = <T>(x: T): T { x };
```

Type parameters are monomorphized at compile time. Recursive and nested generics
are supported.

## 6. Type inference rules

- If a name has a type annotation, it must unify with the inferred type.
- Function parameters without annotations get monomorphic type variables.
- Let-bound names are generalized if their type does not depend on local type
  variables.
- The type of `main` must be `IO<()>` (or a type that unifies with it).

## 7. Typing examples

```helium
a = 0;              // i32
b: i32 = 0;         // i32, explicit
c = 1.0;            // f64
d: f32 = 1.0;       // f32, explicit
e: f64 = 1.0;       // f64
f = true;           // bool
g: bool = false;    // bool
h = "Hello!";       // str
i: str = f"x={a}"; // str
j = [1, 2, 3, 4, 5];            // [i32; 5]
k: [i32; 5] = [1, 2, 3, 4, 5];  // [i32; 5], explicit

l: Option<Option<i32>> = None;  // nested generics
```

## 8. Type errors

The compiler must reject:

- Mismatched primitive types without a legal implicit conversion.
- Array literals whose size or element type do not match the annotation.
- Record literals with missing or extra fields.
- ADT constructors used with wrong payloads.
- Functions applied to the wrong number or types of arguments.
- Unbound type variables escaping their scope.
