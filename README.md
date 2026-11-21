# klep compiler
Multi-Stage SSA Compiler in C (In Progress)

A statically typed compiler written in C targeting QBE.

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
