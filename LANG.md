# Language

## type system

### built-in types

*primitive types:*
- `Int8` = 8-bit signed integer
- `Int16` = 16-bit signed integer
- `Int32` = 32-bit signed integer
- `Int64` = 64-bit signed integer

- `Uint8` = 8-bit unsigned integer
- `Uint16` = 16-bit unsigned integer
- `Uint32` = 32-bit unsigned integer
- `Uint64` = 64-bit unsigned integer

- `Float32` = 32-bit floating point number
- `Float64` = 64-bit floating point number

- `Char` (implemented as `Uint8`)
- `Bool` (implemented as `Uint8`, `true` is non-zero value and `false` is zero)

*aliases*
- `Int` (equivalent of `Int32`)
- `Uint` (equivalent of `Uint32`)
- `Float` (equivalent of `Float32`)

*storage types:*
- `Array<T, const N>` = stack array (eq. of C's `T[const N]`)
- `List<T>` = dynamic heap based array
- `Ref<T>` = reference (pointer under the hood)
- `Box<T>` = owned reference (pointer which owns the data, data strictly in heap)
- `Slice<T>` = reference with list size (pointer+size, used to index without owning data)


## type ownership model

| Type                | Storage             | Ownership     | Freed by          |
|---------------------|---------------------|---------------|-------------------|
| `primitives`        | stack / inline      | copy          | scope exit        |
| `structs`/`enums`   | stack / inline      | move          | scope exit        |
| `Array<T, const N>` | stack / inline      | move          | scope exit        |
| `List<T>`           | heap                | unique, owned | scope exit (RAII) |
| `Ref<T>`            | —                   | non-owning    | n/a               |
| `Box<T>`            | heap                | unique, owned | scope exit (RAII) |
| `Slice<T>`          | fat ptr (ptr + len) | non-owning    | n/a               |

## general Ref/Slice rule
Any `Ref` or `Slice` is valid only within scope of referenced value.
Special case is when a function returns a `Ref`, `Slice` or struct/enum containing such, then a bound function argument needs to be specified.

## auto-ref and auto-deref
> applies in any expr

```text
Int a = 5;
Ref<Int> ra = a;
Int c = ra;

Box<Int> bx = box(c);
Int ubx = bx;
```

### structs

```text
struct TripleInt {
    Int a;
    Int b;
    Int c;
}

struct TwoTripleInt {
    Array<TripleInt, 2> values;
}

```


### enums

```text
enum Color {
    Red,
    Green,
    Blue,
    Any(Uint8, Uint8, Uint8)
}
```


### generic structs

```text
generic struct Pair<A, B> {
    A a;
    B b;
}

generic struct ListViewer<L> {
    Ref<List<L>> list;
    Uint32 position;
}
```

### generic enums

```text
generic enum Option<T> {
    Some(T),
    None
}

generic enum Result<T, E> {
    Ok(T),
    Err(E)
}
```

### extern

#### external functions:
```text
extern Int32 external_fn(Int32* array, Uint32 len)
  into
  Int32 external_fn(Slice<Int32> array { array, len });

extern Int32* alloc_fn(Uint32 len)
  into
  List<Int32> alloc_fn(Uint32 len);

extern void read_fn(Int32* buf, Uint32* out_len)
  into
  Slice<Int32> { buf, out_len } read_fn();
```



#### external types:
> an extern type cannot be dereferenced, constructed, or sized — because the layout is unknown
Useful when value needs to be passed only as a reference from and to external functions. (e.g. window object)  
```text
extern type Ctx;

extern void* create_ctx()
  into Ref<Ctx> create_ctx();
```

## functions

general syntax is `<type> <name>(<args>) { <stmts> }` 
where `<args>` is zero or more arguments in format `<type> <name>` and separated by a comma


Type `Void` can be especially used only as return type of a function, 
declaring that such function does not return any value. 


examples:
```text
Int main(Slice<Slice<Char>> args) {
    Uint n = 0;
    while n < args.len {
        Slice<Char> arg = args[n];
        print("arg ", n+1, " is '", arg, "'\n");
        n += 1;
    }
    return 0;
}

Void exit_successfully() {
    exit(0);
}
```

### bound function arg for reference returning functions
> for now, only one argument can be marked as bound

example:
```text
Ref<Int> index(bound Slice<Int> list, Uint index) {
    return list[index];
}
```


## generic functions
> each use with different type parameters causes copy of the function to be typechecked and added to the output executable.
> (lazy monomorphization)


general syntax is `generic <type> <function name> <type params> (<args>) { <stmts> }`,
where `<type params>` is `<` + one or more types separated by commas + `>`.

```text
generic T add<T>(T a, T b) {
    return a + b;
}
```

Using function `add` from the example with types `Int32` and `Float32` 
will cause the function to exist in the resulting executable twice, each time compiled for that type.

For use, with previously unused type parameters, the compiler performs checks, typechecking, inference, etc. again, 
and could find issues with ownership and types, for example using function `add` from the example with type that does not support addition. 

## statements

### variable declaration

general syntax is `<type> <name> = <expr>;`

examples:
```text
Int a = 1;
Uint16 b = (100 + 3) * 5;
List<Int> l = [1,2,3]; 
```

### variable value change

general syntax is `<variable name>[<accessor>] <op> <expr>;`
where `<accesor>` is either index access `[<expr>]` or field access `.<field name>`,
and `<op>` is `=`, `+=`, `-=`, `*=`, or `/=`.

examples:
```text
a = 10;
b += a;
c.index = 2;
list[3] = list[2];
k.val[0].work.do[n + 4] = "Hi";
```

### string literals

Any string literal is compiled into type inferred by destination type.
Valid destination types for string literals are `Slice<Char>`, `Array<Char, const N>`, and `List<Char>`.


### cast

general syntax is `cast<T>(<expr>)`, and can be used as value inside expressions


examples:
```text
Int a = 3;
Uint b = cast<Uint>(a * 2) + 1;
Char c = cast<Char>(a);
```

### box construction

general syntax is `box(<expr>)`

examples:
```text
Int a = 5;
Box<Int> b = box(a);
Array<Bool, 3> array = [true, true, false];
Box<Array<Bool, 3>> box_array = box(array);
```


### struct construction

general syntax is `new <struct name> { [ <field name> = <expr>, ] }`

examples:
```text
Point p1 = new Point { x=10, y=20 };
TripleInt ti = new TripleInt { a=10, b=20, c=30 }; 
```

### enum construction

general syntax is `new <enum name> ::<variant> [ ( [<value>, ] ) ]`

examples:
```text
Color c1 = new Color::Red;
Color c2 = new Color::Any(255, 255, 0);
```

### if/else if/else

general syntax is `if <expr> { <stmts> } [ else if <expr> { <stmts> } ] [ else { <stmts> } ]`

examples:
```text
// single if
if 1 < 3 {
    print("hi");
}

// if + else
if a + 1 < 5 && b >= 3 {
    a += 1;
} else {
    b += 1;
}

// if + 2x else if + else
if a == 1 {
    b = 2*a;
} else if a == 2 {
    b = a / 2;
} else if a == 3 {
    b = a + 3;
} else {
    print("error, a is ", a, "\n");
    exit(1);
}
```

### while

general syntax is `while <expr> { <stmts> }`

example:
```text
Uint i = 0;
while i < 10 {
    i += 1;
    print("i is ", i);
}
```

### match

general syntax is `match <expr> { [ <val or enum pattern> => { <stmts> } ] }`

```text
match x {
    0 => {
        print_str("zero");
        x += 1;
    }
    1 => {
        print_str("one");
        x += 2;
    }
    2 => {
        print_str("two");
        x += 2;
    }
    n => {
        print_str("any");
    }
}

enum Color {
    Red,
    Green,
    Blue,
    Any(Uint8, Uint8, Uint8)
}

Color c = new Color::Blue;
match c {
    Color::Red => { print("Red"); }
    Color::Green => { print("Green"); }
    Color::Blue => { print("Blue"); }
    Color::Any(r, g, b) => {
        print("Any color (", r, ", ", g, ", ", b, ")");
    }
}

```

