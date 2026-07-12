# Helium Code Formatting Conventions

These conventions apply to all Helium code in documentation, examples, and tests.

## General

- Use 4 spaces for indentation.
- Keep lines at most 100 characters when possible.
- Separate top-level declarations with a blank line.
- End declarations with a semicolon.

## Bindings

```helium
name = value;
name: Type = value;
```

## Functions

```helium
add = (a: i32, b: i32): i32 {
    a + b
};
```

- Parameters and return types are aligned naturally.
- The body is indented one level.
- The closing brace is on its own line.

## Type definitions

```helium
type Point = { x: f64, y: f64 };

type Shape =
    | Circle { center: Point, radius: f64 }
    | Rectangle { top_left: Point, bottom_right: Point }
    ;
```

## Match

```helium
match value {
    Pattern1 => expr1,
    Pattern2 => expr2
}
```

## Loop

```helium
loop (acc = 0, i: i32 = 0) {
    if (i >= n) {
        acc
    } else {
        recur(acc + i, i + 1)
    }
}
```
