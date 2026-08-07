


```text

struct A {
    int32 a;
    uint64 b;
}

// String --> List<Char>
// List<T> --> struct { T** v; uint64 len; uint64 cap; }
// Array<T> --> struct { T* v; uint64 len; }

/*
Int8
Int16
Int32
Int64

Uint8
Uint16
Uint32
Uint64

Char
String
List<T>
Array<T>
Ref<T>

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