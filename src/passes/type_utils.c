#include "type_utils.h"


bool is_type_eq(const TC_typeid* a, const TC_typeid* b) {
    if ((ptrdiff_t) a == (ptrdiff_t) b) return true;
    if (a->ty != b->ty) return false;
    switch (a->ty) {
        case TC_TY_Void: return true;
        case TC_TY_Primitive: return a->info.primitive == b->info.primitive;
        case TC_TY_Struct:
        case TC_TY_Enum: return a->info.type_id == b->info.type_id;
        case TC_TY_Ref:
        case TC_TY_List:
        case TC_TY_Box:
        case TC_TY_Slice: return is_type_eq(a->info.contained, b->info.contained);
        case TC_TY_Array: {
            if (a->info.t_array.array_size != b->info.t_array.array_size) return false;
            return is_type_eq(a->info.t_array.contained, b->info.t_array.contained);
        }
        case TC_TY_External_Type: return a->info.external_type.identifier == b->info.external_type.identifier;
    }
    INTERNAL_ERROR("control flow error");
}