


```text

struct A {
    int32 a;
    uint64 b;
}

// String --> List<Char>
// List<T> --> struct { T** v; uint64 len; uint64 cap; }
// Array<T> --> struct { T* v; uint64 len; }
// Box<T> --> T* (on heap)

/*
Int8
Int16
Int32
Int64

Uint8
Uint16
Uint32
Uint64

Float32
Float64

Char
String
List<T>
Array<T>
Ref<T>
Box<T>

*/

int main(Array<String> args) {
    uint32 i = 0;
    while i < args.len {
        println("arg", i+1, " = ", args[i]);
        i += 1;
    }
    return 0;
}

```



## Compiler types

| Type | Storage | Ownership | Freed by |
| ------ | ---------| ------------- | -------|
| `primitives` | stack / inline | copy | scope exit |
| `structs` | stack / inline | move | scope exit |
| `Array<T>` | heap | unique, owned | scope exit (RAII) |
| `List<T>` | heap | unique, owned | scope exit (RAII) |
| `Ref<T>` | — | non-owning | n/a |
| `Box<T>` | heap | unique, owned | scope exit (RAII) |

### built-in types

- `Int8`
- `Int16`
- `Int32`
- `Int64`
- `Uint8`
- `Uint16`
- `Uint32`
- `Uint64`
- `Float32`
- `Float64`
- `Char`


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