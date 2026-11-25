
#include "utils.h"


bool CMP_tc_typeid(const TC_typeid* restrict a, const TC_typeid* restrict b) {
    if (a->ty != b->ty) return false;
    switch (a->ty) {
        case TC_TY_Void: return true;
        case TC_TY_Primitive: return a->info.primitive == b->info.primitive;
        case TC_TY_Struct:
        case TC_TY_Enum: return a->info.type_id == b->info.type_id;
        case TC_TY_Ref:
        case TC_TY_List:
        case TC_TY_Box:
        case TC_TY_Slice: {
            return CMP_tc_typeid(a->info.contained, b->info.contained);
        }
        case TC_TY_Array: {
            return a->info.t_array.array_size == b->info.t_array.array_size && CMP_tc_typeid(
                       a->info.t_array.contained, b->info.t_array.contained);
        }
        case TC_TY_External_Type: return a->info.external_type.identifier == b->info.external_type.identifier;
    }
    UNREACHABLE()
}











// /// on success returns TRUE, and CAST from source to dest can be emitted
// bool try_to_cast_to(const TC_typeid* source, const TC_typeid* dest) {
//     switch (source->ty) {
//         case TC_TY_Primitive: {
//             if (dest->ty != TC_TY_Primitive && dest->ty != TC_TY_Enum) return false;
//             // dest->info.primitive;
//             return true;
//         }
//         case TC_TY_Enum: {
//             if (dest->ty != TC_TY_Primitive && dest->ty != TC_TY_Enum) return false;
//             UNIMPLEMENTED("try_to_cast_to for TC_TY_Enum")
//         }
//
//         case TC_TY_Void:
//         case TC_TY_Struct:
//         case TC_TY_Ref:
//         case TC_TY_Array:
//         case TC_TY_List:
//         case TC_TY_Box:
//         case TC_TY_Slice:
//         case TC_TY_External_Type:
//             return false;
//     }
//     UNREACHABLE()
// }






// void RAII_emit_typed_destructor(
//     const TC_typeid* restrict type,
//     const size_t in_obj_ptr_temporal,
//     const size_t block_id,
//     const size_t instr_nth,
//     const TC_TypeTable* restrict nct,
//     TC_Entry_LIST* restrict temporals_map,
//     TC_typeid_LIST* restrict type_needing_destructor,
//     const khash_t(sv_u64_map)* restrict identifier_map
// ) {
//     NEW_INSTR_BEGIN
//     printf("NEW_INSTR: CALL_TYPE_DESTRUCTOR{");
//     print_TC_typeid(type, identifier_map, nct);
//     printf("} %%%lu\n", in_obj_ptr_temporal);
//     NEW_INSTR_END
//
//     TC_typeid_LIST_push(type_needing_destructor, *type);
// }
//
// void RAII_drop(
//     const TC_typeid* restrict type,
//     const size_t in_obj_ptr_temporal,
//     const size_t block_id,
//     const size_t instr_nth,
//     const TC_TypeTable* restrict nct,
//     TC_Entry_LIST* restrict temporals_map,
//     TC_typeid_LIST* restrict type_needing_destructor,
//     const khash_t(sv_u64_map)* restrict identifier_map
// ) {
//     switch (type->ty) {
//         case TC_TY_Void:
//         case TC_TY_Primitive:
//         case TC_TY_Ref:
//         case TC_TY_Slice: {
//             NEW_INSTR_BEGIN
//             printf("NEW_INSTR: FREE_HEAP_PTR %%%lu\n", in_obj_ptr_temporal);
//             NEW_INSTR_END
//         }
//         case TC_TY_External_Type: return;
//
//
//         case TC_TY_Box: {
//             // BOX is just an owned heap ptr
//             RAII_drop(type->info.contained, in_obj_ptr_temporal, block_id, instr_nth, nct, temporals_map,
//                       type_needing_destructor, identifier_map);
//             break;
//         }
//
//         case TC_TY_List:
//         case TC_TY_Struct:
//         case TC_TY_Enum:
//         case TC_TY_Array: {
//             RAII_emit_typed_destructor(type, in_obj_ptr_temporal, block_id, instr_nth, nct, temporals_map,
//                                        type_needing_destructor, identifier_map);
//             break;
//         }
//     }
// }






SizeAlign calc_size_of(const TC_typeid* type, const TC_TypeTable* restrict nct,
                       const TargetInfo* restrict target_info) {
    switch (type->ty) {
        case TC_TY_Void: INTERNAL_ERROR("SIZEOF_ VOID")
        case TC_TY_Primitive: {
            return (SizeAlign){TC_Primitive_TY_size(type->info.primitive), TC_Primitive_TY_size(type->info.primitive)};
        }
        case TC_TY_Slice: {
            return (SizeAlign){target_info->ptr_size + 4, target_info->ptr_size}; // { ptr; len; }
        }
        case TC_TY_Struct: {
            return (SizeAlign){
                nct->structs.array[type->info.type_id].layout.size, nct->structs.array[type->info.type_id].layout.align
            };
        }
        case TC_TY_Enum: {
            return (SizeAlign){nct->enums.array[type->info.type_id].size, nct->enums.array[type->info.type_id].align};
        }
        case TC_TY_Ref:
        case TC_TY_Box: {
            return (SizeAlign){target_info->ptr_size, target_info->ptr_size};
        }
        case TC_TY_Array: {
            const SizeAlign inner = calc_size_of(type->info.t_array.contained, nct, target_info);
            return (SizeAlign){
                type->info.t_array.array_size * inner.size,
                inner.align
            };
        }
        case TC_TY_List: {
            return (SizeAlign){target_info->ptr_size + 4 + 4, target_info->ptr_size}; // { ptr; len; cap; }
        }
        case TC_TY_External_Type: INTERNAL_ERROR("SIZEOF_ EXTERNAL_TYPE")
    }
    UNREACHABLE()
}