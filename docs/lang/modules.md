# Helium Modules and FFI

## 1. Module declaration

A module file starts with an optional module declaration:

```helium
module math;

pi: f64 = 3.14159;
```

If no module declaration is present, the module name is derived from the file
name.

## 2. Imports

Other modules are imported with the `import` statement.

```helium
import math;

area = (r: f64): f64 {
    math.pi * r * r;
};
```

The last component of the import path becomes the namespace prefix. Qualified
access is required: `math.pi`. Modules expose all top-level names by default;
visibility modifiers are not required in the first version but may be added
later.

## 3. Module paths

Imports follow the package structure. The package manager resolves dependencies
and makes their modules available to the compiler through the `.helium/`
cache.

## 4. FFI

Foreign functions are declared with a foreign signature. The compiler emits the
necessary calling-convention glue. From the caller's perspective, a foreign
function is an ordinary Helium function.

Foreign functions are declared with the `foreign` keyword, a name, an optional
list of type parameters, a colon, and a Helium function type:

```helium
foreign puts : fn(str) -> i32;
foreign length<T> : fn([T; 0]) -> i32;
```

The standard library uses FFI to wrap C functions. `io.println` is not a
builtin; it is a library function that calls a foreign `puts`-like function
through the FFI.

## 5. Compilation units

Each `.hel` file is a compilation unit. The compiler produces interface metadata
for each compiled module so that dependents can type-check against it without
re-reading the source.

## 6. Standard library

The standard library is a normal package named `std`. It is not special-cased
by the compiler except that the package manager may provide a convenient way to
reference it.

```helium
import std.io;

main = () : IO<()> {
    io.println("Hello");
}
```

`main` may also accept command-line arguments as an array of strings:

```helium
import std.io;

main = (args: [str]) : IO<()> {
    io.println("Arguments:");
}
```

The supported entry-point signatures are:

- `main: IO<()>` — an IO action.
- `main: fn() -> IO<()>` — a nullary function returning an IO action.
- `main: fn([str]) -> IO<()>` — a function that receives command-line
  arguments (excluding the program name) and returns an IO action.

For lambdas, the following equivalent forms are also accepted:

```helium
main = () : IO<()> { ... }
main = (args: [str]) : IO<()> { ... }
```

## 7. Invalid module usage

The compiler must reject:

- Importing a module that cannot be found.
- Accessing a name that is not exported by an imported module.
- Type mismatches when calling functions from another module.
