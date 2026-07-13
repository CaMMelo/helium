# Helium Expressions

## 1. Bindings

There are no assignment statements. A binding gives a name to a value exactly
once in its scope.

```helium
x = 42;
y: i32 = 42;
```

Rebinding the same name in the same scope is an error. Shadowing in nested
scopes is allowed.

## 2. Blocks

A block is a sequence of bindings and expressions delimited by braces. The last
expression is the value of the block.

```helium
x = { 1 };          // x = 1
y = { a = 1; a };   // y = 1
empty = {};         // empty = ()
```

Inside a block, semicolons separate bindings and expressions. The last
expression is the value of the block. A trailing semicolon before the closing
`}` is **not** accepted, matching the grammar implemented in SPEC-002:

```helium
z = {
    a = 1;
    a
};
```

## 3. Conditionals

`if` is an expression and must have an `else` branch when its value is used.

```helium
a = if (true) {
    32;
} else {
    42;
};
```

The `else` branch is required when the `if` appears in an expression position.
If the `if` is used only for effects inside an `IO` block, both branches must
still have a type consistent with the surrounding context.

## 4. Pattern matching

`match` inspects an ADT or primitive value and selects an arm.

```helium
area = (s: Shape): f64 {
    match s {
        Circle { radius: r } => 3.14 * r * r,
        Rectangle { top_left: tl, bottom_right: br } =>
            (br.x - tl.x) * (br.y - tl.y)
    }
};
```

Match arms are separated by commas. The last comma is optional. All arms must
return the same type. Patterns may bind names from record fields.

## 5. Loops

Loops are syntax sugar for tail recursion. The `loop` expression introduces
bindings that are updated by `recur`.

```helium
sum_loop = (n: i32): i32 {
    loop (acc = 0, i: i32 = 0) {
        if (i >= n) {
            acc
        } else {
            recur(acc + i, i + 1)
        }
    }
};
```

`loop` bindings may omit types; they are inferred from the initializer. `recur`
must receive the same number and order of arguments as the `loop` bindings.

## 6. Function literals

A function literal (lambda) is written as an ordinary function declaration
without a name:

```helium
inc = (n: i32): i32 { n + 1 };
```

Functions are first-class values.

## 7. Operator expressions

All binary and unary operators follow C precedence and associativity. The
monadic bind operator `>>=` is left-associative and lower precedence than `||`.

## 8. Invalid expressions

The compiler must reject:

- Using an `if` expression without `else` in a value position.
- A `match` with missing variants for an ADT.
- `recur` outside of a `loop`.
- `recur` with the wrong number or type of arguments.
- Rebinding a name in the same scope.
