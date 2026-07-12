# Helium Syntax

## 1. Source files

- Source files use the extension `.hel`.
- UTF-8 encoded.
- Statements and declarations are separated by semicolons `;`.
- A trailing semicolon is allowed before a closing brace.

## 2. Comments

C-style comments only:

```helium
// line comment

/*
   block comment
*/
```

Nested block comments are not required in the first version.

## 3. Identifiers

- Start with a letter or underscore.
- Continue with letters, digits, or underscores.
- Case-sensitive.
- Type identifiers and constructor identifiers start with an uppercase letter.
- Value identifiers and function parameters start with a lowercase letter.

Examples: `x`, `_unused`, `Point`, `Some`, `io`, `main`.

## 4. Keywords

No binding keywords. The following are reserved:

```
if      else    match
type    module  import
loop    recur
fn
```

`return` is allowed but optional; the last expression of a block is the
implicit return value.

## 5. Literals

| Kind | Examples | Notes |
|------|----------|-------|
| integer | `0`, `42`, `-7` | Signed; smallest fitting type inferred (`i32` default) |
| float | `1.0`, `3.14` | Inferred to `f64` unless annotated |
| bool | `true`, `false` | Type `bool` |
| string | `"hello"` | Type `str`; immutable UTF-8 |
| unit | `()` | Type `()` |

String literals support Python-style interpolation with `f"..."`:

```helium
a = 42;
m = f"value is {a}";
```

Interpolated expressions must be single expressions; they can be parenthesized
for clarity.

F-strings are a language-level feature. There is no separate string
concatenation operator or function; interpolation is the only way to compose
strings.

## 6. Operators

Use standard C precedence and associativity:

| Precedence | Operators | Associativity |
|------------|-----------|---------------|
| 1 (highest) | `()` `[]` `.` | left |
| 2 | unary `+` `-` `!` | right |
| 3 | `*` `/` `%` | left |
| 4 | `+` `-` | left |
| 5 | `<` `<=` `>` `>=` | left |
| 6 | `==` `!=` | left |
| 7 | `&&` | left |
| 8 | `||` | left |
| 9 | `>>=` (bind) | left |
| 10 | `=` (binding, not assignment) | N/A at expression level |

`>>=` is the monadic bind operator.

## 7. Delimiters

```
( )   grouping, tuples, parameter lists
[ ; ] arrays with element type and size: [i32; 5]
{ }   blocks, records, record literals
|     ADT variant separator
=>    match arm arrow
:     type annotation
,     separator
.     field access
```

## 8. Whitespace

Whitespace is insignificant except for separating tokens.

## 9. Example lexeme summary

```helium
// values
42
3.14
true
"hello"
f"x={x}"

// names
x
Point
Some
io

// grouping
(a + b)
[1, 2, 3]
Point{ x: 1.0, y: 2.0 }

// blocks
{ a = 1; a }
```
