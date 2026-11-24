# klep compiler

**Klep** is a statically typed programming language and multi-stage SSA compiler written in C targeting the [QBE](https://c9x.me) compiler backend. *In Progress)*

## Architecture
- Frontend & IR: Emits SSA HIR directly during parsing without building an AST or resolving types.
- HIR Analysis: Type inference on temporals and stack slot materialization for address taken or oversized values.
- Middle-End: Progressive lowering from SSA HIR to SSA MIR with static type checking, lazy monomorphization for generics, and an RAII verification pass.
- Backend: Lowers verified SSA MIR to QBE IR.

## in progress
not implemented:
- RAII verification pass
- partial QBE IR emission
- `Slice<T>,List<T>,...` built-in types are not correctly translated from `HIR` to `MIR`

## usage

```text
Usage: klep [options] <input.klep>

Options:
-o <file>, --output=<file>  Set output file path (default: C compiler default)
--emit-ssa <path>           Emit QBE SSA IR to file (default: empty)
--emit-asm <path>           Emit assembly to file (default: empty)
--qbe-bin <path>            Set QBE binary path (default: qbe)
--cc-bin <path>             Set C compiler binary path (default: gcc)
--qbe-target <target>       Set QBE target (default: platform default)
--cc-args <args>            Extra arguments passed to C compiler (default: empty)
-h, --help                  Show this help message and exit
-v, --version               Show compiler version and exit

Arguments:
<input.klep>                The Klep source file to compile (required)
```

example of cross-platform compilation
```shell
klep programs/out_test.klep --cc-bin="x86_64-w64-mingw32-cc" --qbe-target="amd64_win" -o out.exe
```

## example program that compiles
```

struct A {
    Int v1;
    Int v2;
}


Int add(Int a, Int b) {
    return a + b;
}

Int add(Ref<A> a) {
    return a.v1 + a.v2;
}


Int main() {
    Int v1 = 5;
    Int v2 = v1 + 3;
    Int res = add(v1, v2);

    A s1 = new A { v1=1, v2=2 };
    Ref<A> s2 = s1;
    Int res1 = add(s1);

    Slice<Char> str = "Hello World!\n";

    Int i = 0;
    while i < 10 {
        i += 1;
    }
    return i;
}

```
Returns with exit number 10.
 
--- 

See `LANG.md` for language specification.
