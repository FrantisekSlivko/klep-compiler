# Language


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

| Type          | Storage             | Ownership     | Freed by          |
|---------------|---------------------|---------------|-------------------|
| `primitives`  | stack / inline      | copy          | scope exit        |
| `structs`     | stack / inline      | move          | scope exit        |
| `Array<T, N>` | stack / inline      | move          | scope exit        |
| `List<T>`     | heap                | unique, owned | scope exit (RAII) |
| `Ref<T>`      | —                   | non-owning    | n/a               |
| `Box<T>`      | heap                | unique, owned | scope exit (RAII) |
| `Slice<T>`    | fat ptr (ptr + len) | non-owning    | n/a               |

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

### extern

#### external functions:
```text
// Borrow — C owns the memory
extern Int32 external_fn(Int32* array, Uint32 len)
  into
  Int32 external_fn(Slice<Int32> array { array, len });

// Ownership transfer — your runtime frees
extern Int32* alloc_fn(Uint32 len)
  into
  Array<Int32> alloc_fn(Uint32 len);

// Nullable
extern Int32* maybe_fn(Uint32 len)
  into
  Option<Slice<Int32>> maybe_fn(Uint32 len);

// Output param
extern void read_fn(Int32* buf, Uint32* out_len)
  into
  Slice<Int32> { buf, out_len } read_fn();


extern type Ctx;

extern void* create_ctx()
  into Ref<Ctx> create_ctx()
```



#### external types:
> an extern type cannot be dereferenced, constructed, or sized — because the layout is unknown
```text
extern type Ctx;
```