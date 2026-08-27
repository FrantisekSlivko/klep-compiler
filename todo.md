# TODO:
? what about big return values? → e.g. structs? When do the switch to out ptr?
- args for functions (they are not materialized, not binded, not accessible, and not in scope)
- strings? Char->U8?, CONST_STRING IR? for copy from .rodata?
- match
- extern
- continue
- break


in type resolution → use of generic causes materialization of that type 
    → StructA<Int32> → `material_struct_generics[StructA<Int32>] = Struct { ... }`


