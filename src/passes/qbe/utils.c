#include "utils.h"

#include "typecompletion.h"

// TC_primitive_classification tc_primitive_classification(const TC_Primitive_TY t);

void qbe_fn_call_type(const TC_typeid* restrict t, const TargetInfo* restrict target_info, char* out) {
    switch (t->ty) {
        case TC_TY_Void: INTERNAL_ERROR("void")
        case TC_TY_Primitive: {
            const TC_primitive_classification tpc = tc_primitive_classification(t->info.primitive);
            const char sign = tpc.is_signed_int_primitive ? 's' : 'u';
            out[0] = tpc.is_float_primitive ? ' ' : sign;
            char rt;
            if (tpc.is_float_primitive) {
                rt = tpc.primitive_byte_size == 4 ? 's' : 'd';
            } else {
                switch (tpc.primitive_byte_size) {
                    case 1: { rt = 'b'; break; }
                    case 2: { rt = 'h'; break; }
                    case 4: { rt = 'w'; break; }
                    case 8: { rt = 'l'; break; }
                    default: { INTERNAL_ERROR() }
                }
            }
            out[1] = rt;
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
                out[0] = ' ';
                out[1] = 'l';
                return;
            } else {
                out[0] = ' ';
                out[1] = 'w';
                return;
            }
        }
    }
    UNREACHABLE()

}

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
    UNREACHABLE()
}



TC_primitive_classification tc_primitive_classification(const TC_Primitive_TY t) {
    bool is_float_primitive = false;
    bool is_signed_int_primitive = false;
    uint8_t primitive_byte_size = 0;

    switch (t) {
        case TC_Primitive__Int8:
        case TC_Primitive__Int16:
        case TC_Primitive__Int32:
        case TC_Primitive__Int64: {
            is_signed_int_primitive = true;
            break;
        }
        case TC_Primitive__Uint8:
        case TC_Primitive__Uint16:
        case TC_Primitive__Uint32:
        case TC_Primitive__Uint64:
        case TC_Primitive__Float32:
        case TC_Primitive__Float64:
        case TC_Primitive__Char:
        case TC_Primitive__Bool: {
            is_signed_int_primitive = false;
            break;
        }
    }

    switch (t) {
        case TC_Primitive__Int8:
        case TC_Primitive__Int16:
        case TC_Primitive__Int32:
        case TC_Primitive__Int64:
        case TC_Primitive__Uint8:
        case TC_Primitive__Uint16:
        case TC_Primitive__Uint32:
        case TC_Primitive__Uint64:
        case TC_Primitive__Char:
        case TC_Primitive__Bool: {
            is_float_primitive = false;
            break;
        }
        case TC_Primitive__Float32:
        case TC_Primitive__Float64: {
            is_float_primitive = true;
            break;
        }
    }

    switch (t) {
        case TC_Primitive__Int8:
        case TC_Primitive__Uint8: {
            primitive_byte_size = 1;
            break;
        }

        case TC_Primitive__Int16:
        case TC_Primitive__Uint16: {
            primitive_byte_size = 2;
            break;
        }

        case TC_Primitive__Int32:
        case TC_Primitive__Uint32: {
            primitive_byte_size = 4;
            break;
        }

        case TC_Primitive__Int64:
        case TC_Primitive__Uint64: {
            primitive_byte_size = 8;
            break;
        }

        case TC_Primitive__Char:
        case TC_Primitive__Bool: {
            primitive_byte_size = 1;
            break;
        }

        case TC_Primitive__Float32: {
            primitive_byte_size = 4;
            break;
        }
        case TC_Primitive__Float64: {
            primitive_byte_size = 8;
            break;
        }
    }

    return (TC_primitive_classification) {
        .is_float_primitive = is_float_primitive,
        .is_signed_int_primitive = is_signed_int_primitive,
        .primitive_byte_size = primitive_byte_size
    };
}