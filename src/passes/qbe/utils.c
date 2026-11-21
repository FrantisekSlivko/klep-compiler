#include "utils.h"

#include "typecompletion.h"


/// returns QBE SSA temporary base type (w for 1/2/4-byte integers/bool/char, l for 64-bit ints, pointers, and  aggregate stack-slot pointers, s for Float32, d for Float64).
char qbe_val_type(const TC_typeid* t, const TargetInfo* restrict target_info) {
    switch (t->ty) {
        case TC_TY_Void: INTERNAL_ERROR("void")
        case TC_TY_Primitive: {
            switch (t->info.primitive) {
                case TC_Primitive__Int8:
                case TC_Primitive__Int16:
                case TC_Primitive__Int32:
                    return 'w';
                case TC_Primitive__Int64:
                    return 'l';
                case TC_Primitive__Uint8:
                case TC_Primitive__Uint16:
                case TC_Primitive__Uint32:
                    return 'w';
                case TC_Primitive__Uint64:
                    return 'l';
                case TC_Primitive__Float32:
                    return 's';
                case TC_Primitive__Float64:
                    return 'd';
                case TC_Primitive__Char:
                case TC_Primitive__Bool:
                    return 'w';
            }
            UNREACHABLE()
        }
        case TC_TY_Struct:
        case TC_TY_Enum:
        case TC_TY_Ref:
        case TC_TY_Array:
        case TC_TY_List:
        case TC_TY_Box:
        case TC_TY_Slice:
        case TC_TY_External_Type: {
            if (target_info->ptr_size == 8) {
                return 'l';
            } else {
                return 'w';
            }
        }
    }
}
