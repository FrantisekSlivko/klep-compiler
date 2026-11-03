#include "typechecking.h"

#include <stddef.h>

#include "error.h"
#include "ir.h"
#include "list.h"
#include "typecompletion.h"
#include "utils/dev.h"
#include "utils/color.h"
#include <ctype.h>

#include "mir.h"

#define NEW_INSTR_BEGIN printf(C_GREEN);
#define NEW_INSTR_END printf(C_RESET);


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


SizeAlign path_size_align_for_materialized_type(
    const TC_typeid* restrict type,
    const TC_TypeTable* restrict nct,

    size_t_LIST* restrict in_progress_structs,
    size_t_LIST* restrict in_progress_enums,

    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info
) {
    switch (type->ty) {
        case TC_TY_Void: {
            INTERNAL_ERROR("void type does not have size or align")
        }
        case TC_TY_External_Type: {
            const SV msg = SV_from_str(
                "external type has unknow size and therefore cannot be used alone, use it with for example Ref<T>");
            syntax_error(&source_info->source, &source_info->source_filename, &type->info.external_type.span.sv, &msg);
            exit(1);
        }
        case TC_TY_Primitive: {
            return (SizeAlign){TC_Primitive_TY_size(type->info.primitive), TC_Primitive_TY_size(type->info.primitive)};
        }
        case TC_TY_Slice: {
            return (SizeAlign){target_info->ptr_size * 2, target_info->ptr_size}; // { ptr; len; }
        }
        case TC_TY_Array: {
            const SizeAlign inner = path_size_align_for_materialized_type(
                type->info.t_array.contained, nct, in_progress_structs, in_progress_enums, target_info, source_info);
            return (SizeAlign){
                type->info.t_array.array_size * inner.size,
                inner.align
            };
        }
        case TC_TY_List: {
            return (SizeAlign){target_info->ptr_size * 3, target_info->ptr_size}; // { ptr; len; cap; }
        }
        case TC_TY_Ref:
        case TC_TY_Box: {
            return (SizeAlign){target_info->ptr_size, target_info->ptr_size};
        }
        case TC_TY_Struct: {
            TC_structdef* item = &nct->structs.array[type->info.type_id];

            // already calculated
            if (item->layout.size != 0) {
                return (SizeAlign){item->layout.size, item->layout.align};
            }

            for (size_t n = 0; n < in_progress_structs->len; n++) {
                if (in_progress_structs->array[n] == type->info.type_id) {
                    // cycle detected
                    const SV msg = SV_from_str(
                        "owned cycles are forbidden, use Box<T> if you need to construct cycle in runtime");
                    syntax_error(&source_info->source, &source_info->source_filename, &item->name.span.sv, &msg);
                    exit(1);
                }
            }

            // calc
            size_t_LIST_push(in_progress_structs, type->info.type_id); // mark as in_progress
            const size_t this_in_progress_nth = in_progress_structs->len - 1;

            uint32_t* field_offsets = malloc(sizeof(uint32_t) * item->fields.len);


            uint32_t offset = 0;
            uint32_t max_align = 1;

            for (uint32_t i = 0; i < item->fields.len; i++) {
                const TC_typeid* ft = &item->fields.array[i].type;

                const SizeAlign sa = path_size_align_for_materialized_type(
                    ft, nct, in_progress_structs, in_progress_enums, target_info, source_info);

                const uint32_t fa = sa.align;
                const uint32_t fs = sa.size;

                offset = (offset + fa - 1) & ~(fa - 1); // align up
                field_offsets[i] = offset;
                offset += fs;
                if (fa > max_align) max_align = fa;
            }

            const TC_struct_layout sl = (TC_struct_layout){
                (offset + max_align - 1) & ~(max_align - 1), // tail padding,
                max_align,
                field_offsets
            };
            item->layout = sl; // write to the struct

            // done
            in_progress_structs->array[this_in_progress_nth] = SIZE_MAX;

            return (SizeAlign){item->layout.size, item->layout.align};
        }
        case TC_TY_Enum: {
            TC_enumdef* item = &nct->enums.array[type->info.type_id];

            if (item->size != 0) {
                return (SizeAlign){item->size, item->align};
            }

            for (size_t n = 0; n < in_progress_enums->len; n++) {
                if (in_progress_enums->array[n] == type->info.type_id) {
                    const SV msg = SV_from_str(
                        "owned cycles are forbidden, use Box<T> if you need to construct cycle in runtime");
                    syntax_error(&source_info->source, &source_info->source_filename, &item->name.span.sv, &msg);
                    exit(1);
                }
            }

            size_t_LIST_push(in_progress_enums, type->info.type_id);
            const size_t this_in_progress_nth = in_progress_enums->len - 1;

            // discriminant is a primitive integer, align == size
            size_t disc_size;
            if (item->variants.len <= 0xFF) disc_size = 1;
            else if (item->variants.len <= 0xFFFF) disc_size = 2;
            else if (item->variants.len <= 0xFFFFFFFF) disc_size = 4;
            else disc_size = 8;

            size_t max_payload_size = 0;
            size_t max_payload_align = 1;

            for (size_t v = 0; v < item->variants.len; v++) {
                const TC_enumdef_variant* variant = &item->variants.array[v];

                size_t payload_size = 0;
                size_t variant_align = 1;

                for (size_t f = 0; f < variant->values_len; f++) {
                    const SizeAlign sa = path_size_align_for_materialized_type(
                        &variant->values[f],
                        nct,
                        in_progress_structs, in_progress_enums,
                        target_info, source_info
                    );
                    payload_size = (payload_size + sa.align - 1) & ~(sa.align - 1);
                    payload_size += sa.size;
                    if (sa.align > variant_align) variant_align = sa.align;
                }

                // round variant payload up to its own alignment
                payload_size = (payload_size + variant_align - 1) & ~(variant_align - 1);

                if (payload_size > max_payload_size) max_payload_size = payload_size;
                if (variant_align > max_payload_align) max_payload_align = variant_align;
            }

            const size_t total_align = disc_size > max_payload_align ? disc_size : max_payload_align;
            const size_t payload_start = (disc_size + max_payload_align - 1) & ~(max_payload_align - 1);
            const size_t raw_size = payload_start + max_payload_size;
            const size_t total_size = (raw_size + total_align - 1) & ~(total_align - 1);

            item->size = total_size;
            item->align = total_align;
            item->discriminant_size = disc_size;

            in_progress_enums->array[this_in_progress_nth] = SIZE_MAX;

            return (SizeAlign){total_size, total_align};
        }
    }
    UNREACHABLE()
}


TC_typeid resolve_tc_type(
    const IR_typeuse* restrict type,
    TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info
) {
    switch (type->tt_ty) {
        case IR_TY_Void: {
            return (TC_typeid){
                .ty = TC_TY_Void,
                .full_typeuse_span = type->full_typeuse_span
            };
        }
        case IR_TY_Primitive: {
            return (TC_typeid){
                .ty = TC_TY_Primitive,
                .info.primitive = type->info.primitive,
                .full_typeuse_span = type->full_typeuse_span
            };
        }
        case IR_TY_Named: {
            for (size_t n = 0; n < nct->enums.len; n++) {
                if (nct->enums.array[n].name.identifier == type->info.named.identifier) {
                    return (TC_typeid){
                        .ty = TC_TY_Enum,
                        .info.type_id = n,
                        .full_typeuse_span = type->full_typeuse_span
                    };
                }
            }
            for (size_t n = 0; n < nct->structs.len; n++) {
                if (nct->structs.array[n].name.identifier == type->info.named.identifier) {
                    return (TC_typeid){
                        .ty = TC_TY_Struct,
                        .info.type_id = n,
                        .full_typeuse_span = type->full_typeuse_span
                    };
                }
            }
            for (size_t n = 0; n < nct->external_types.len; n++) {
                if (nct->external_types.array[n].identifier == type->info.named.identifier) {
                    return (TC_typeid){
                        .ty = TC_TY_External_Type,
                        .info.external_type = type->info.named,
                        .full_typeuse_span = type->full_typeuse_span
                    };
                }
            }

            const SV msg = SV_from_str_len("unknow type", 11);
            syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.span.sv, &msg);
            if (SV_pv_cmp_eq(&type->info.named.span.sv, "String", 6)) {
                const SV msg2 = SV_from_str(
                    "String as type does not exist, you can use "C_BOLD"Slice<Char>"C_RESET", "C_BOLD"List<Char>"C_RESET
                    ", "C_BOLD"Array<Char, N>"C_RESET" instead");
                hint_standalone(&msg2);
            }
            exit(1);
        }
        case IR_TY_Array: {
            TC_typeid* contained = malloc(sizeof(TC_typeid));
            *contained = resolve_tc_type(type->contained, nct, target_info, source_info);
            TC_type_ty ty = TC_TY_Array;
            return (TC_typeid){
                .ty = ty,
                .info.t_array = {contained, type->array_const_size},
                .full_typeuse_span = type->full_typeuse_span
            };
            break;
        }
        case IR_TY_Ref:
        case IR_TY_Box:
        case IR_TY_Slice:
        case IR_TY_List: {
            TC_typeid* contained = malloc(sizeof(TC_typeid));
            *contained = resolve_tc_type(type->contained, nct, target_info, source_info);

            // free(type->contained);

            TC_type_ty ty;
            switch (type->tt_ty) {
                case IR_TY_Ref: {
                    ty = TC_TY_Ref;
                    break;
                }
                case IR_TY_List: {
                    ty = TC_TY_List;
                    break;
                }
                case IR_TY_Box: {
                    ty = TC_TY_Box;
                    break;
                }
                case IR_TY_Slice: {
                    ty = TC_TY_Slice;
                    break;
                }
                default: {
                    UNREACHABLE();
                }
            }

            return (TC_typeid){
                .ty = ty,
                .info.contained = contained,
                .full_typeuse_span = type->full_typeuse_span
            };
        }
        case IR_TY_Generic: {
            TC_index_record_kind materialize_kind;
            size_t materialize_nth;
            // WRONG, on first same generic diff args, triggers materialization despite the type could be later in materialized

            for (size_t n = 0; n < nct->materialized_generics.len; n++) {
                const TC_materialized_generic_index_record mr = nct->materialized_generics.array[n];
                if (mr.generic.tt_ty != IR_TY_Generic) {
                    INTERNAL_ERROR("should never happen | u403");
                }

                const IR_typeuse tu = mr.generic;
                if (tu.info.named.identifier != type->info.named.identifier) {
                    continue;
                }

                if (tu.contained_len != type->contained_len) {
                    char* r_msg = malloc(sizeof(char) * 100);
                    snprintf(r_msg, 100, "for generic expected %lu args, but got %lu", tu.contained_len,
                             type->contained_len);
                    const SV msg = SV_from_str(r_msg);
                    syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.span.sv, &msg);
                    free(r_msg);
                    exit(1);
                }

                // bool args_match = false;
                bool arguments_are_identical = true;
                for (size_t j = 0; j < tu.contained_len; j++) {
                    // if (!CMP_IR_typeuse(&tu.contained[j], &type->contained[j])) {
                    //     args_match = true;
                    //     break;
                    // }
                    // 1. Resolve BOTH arguments into their true semantic types
                    TC_typeid cached_arg = resolve_tc_type(&tu.contained[j], nct, target_info, source_info);
                    TC_typeid new_arg = resolve_tc_type(&type->contained[j], nct, target_info, source_info);

                    // 2. Compare the semantic types!
                    // This perfectly bridges the gap if one was parsed as Named and one as Primitive.
                    if (!is_type_eq(&cached_arg, &new_arg)) {
                        arguments_are_identical = false;
                        break;
                    }
                }
                // if (args_match) {
                // continue;
                // }
                if (!arguments_are_identical) {
                    continue; // Mismatch! Move on to check the next generic in the cache.
                }

                TC_type_ty ty = TC_TY_Struct;
                switch (mr.kind) {
                    case TC_NIRK_Enum: {
                        ty = TC_TY_Enum;
                        break;
                    }
                    case TC_NIRK_Struct: {
                        ty = TC_TY_Struct;
                        break;
                    }
                }
                return (TC_typeid){
                    .ty = ty,
                    .info.type_id = mr.nth,
                    .full_typeuse_span = type->full_typeuse_span
                };
            }

            for (size_t n = 0; n < nct->generic_enums.len; n++) {
                if (nct->generic_enums.array[n].name.identifier == type->info.named.identifier) {
                    if (nct->generic_enums.array[n].generic_args.len != type->contained_len) {
                        char* r_msg = malloc(sizeof(char) * 100);
                        snprintf(r_msg, 100, "for generic expected %lu args, but got %lu",
                                 nct->generic_enums.array[n].generic_args.len, type->contained_len);
                        const SV msg = SV_from_str(r_msg);
                        syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.span.sv,
                                     &msg);
                        free(r_msg);
                        exit(1);
                    }
                    materialize_kind = TC_NIRK_Enum;
                    materialize_nth = n;
                    goto materialize;
                }
            }

            for (size_t n = 0; n < nct->generic_structs.len; n++) {
                if (nct->generic_structs.array[n].name.identifier == type->info.named.identifier) {
                    if (nct->generic_structs.array[n].generic_args.len != type->contained_len) {
                        char* r_msg = malloc(sizeof(char) * 100);
                        snprintf(r_msg, 100, "for generic expected %lu args, but got %lu",
                                 nct->generic_structs.array[n].generic_args.len, type->contained_len);
                        const SV msg = SV_from_str(r_msg);
                        syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.span.sv,
                                     &msg);
                        free(r_msg);
                        exit(1);
                    }
                    materialize_kind = TC_NIRK_Struct;
                    materialize_nth = n;
                    goto materialize;
                }
            }

            {
                const SV msg = SV_from_str("unknow generic");
                syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.span.sv, &msg);
                exit(1);
            }

            // materialize
        materialize: {
                switch (materialize_kind) {
                    case TC_NIRK_Enum: {
                        IR_generic_enumdef ged = nct->generic_enums.array[materialize_nth];

                        // materialize
                        TC_enumdef_variant_LIST new_variants = TC_enumdef_variant_LIST_new_with_capacity(
                            ged.variants.len);
                        for (size_t j = 0; j < ged.variants.len; j++) {
                            const IR_enumdef_variant variant = ged.variants.array[j];
                            TC_typeid* new_values = malloc(sizeof(TC_typeid) * variant.values_len);

                            for (size_t v = 0; v < variant.values_len; v++) {
                                const IR_typeuse substituted = substitute_typeuse(
                                    &variant.values[v], &ged.generic_args, type->contained);
                                const TC_typeid ty = resolve_tc_type(&substituted, nct, target_info, source_info);
                                new_values[v] = ty;
                            }

                            TC_enumdef_variant_LIST_push(&new_variants, (TC_enumdef_variant){
                                                             variant.name,
                                                             new_values,
                                                             variant.values_len
                                                         });
                        }

                        const TC_enumdef new_enumdef = (TC_enumdef){
                            ged.name,
                            new_variants,
                            0,
                            0,
                            0
                        };


                        const size_t nth = nct->enums.len;
                        TC_enumdef_LIST_push(&nct->enums, new_enumdef);
                        TC_materialized_generic_index_record_LIST_push(&nct->materialized_generics,
                                                                       (TC_materialized_generic_index_record){
                                                                           *type,
                                                                           TC_NIRK_Enum,
                                                                           nth
                                                                       });

                        const TC_typeid ret = (TC_typeid){
                            .ty = TC_TY_Enum,
                            .info.type_id = nth,
                            .full_typeuse_span = type->full_typeuse_span
                        };

                        {
                            size_t_LIST in_progress_structs = size_t_LIST_new();
                            size_t_LIST in_progress_enums = size_t_LIST_new();

                            path_size_align_for_materialized_type(&ret, nct, &in_progress_structs, &in_progress_enums,
                                                                  target_info, source_info);

                            size_t_LIST_free(&in_progress_structs);
                            size_t_LIST_free(&in_progress_enums);
                        }

                        return ret;
                    }
                    case TC_NIRK_Struct: {
                        IR_generic_structdef ges = nct->generic_structs.array[materialize_nth];


                        TC_field_LIST new_fields = TC_field_LIST_new_with_capacity(ges.fields.len);
                        new_fields.len = ges.fields.len;

                        for (size_t j = 0; j < ges.fields.len; j++) {
                            const IR_field fd = ges.fields.array[j];
                            const IR_typeuse substituted = substitute_typeuse(
                                &fd.type, &ges.generic_args, type->contained);
                            const TC_typeid ty = resolve_tc_type(&substituted, nct, target_info, source_info);
                            new_fields.array[j] = (TC_field){
                                fd.name,
                                ty
                            };
                        }

                        const TC_structdef new_structdef = (TC_structdef){
                            ges.name,
                            new_fields,
                            (TC_struct_layout){
                                0,
                                0,
                                NULL
                            }
                        };

                        const size_t nth = nct->structs.len;
                        TC_structdef_LIST_push(&nct->structs, new_structdef);
                        TC_materialized_generic_index_record_LIST_push(&nct->materialized_generics,
                                                                       (TC_materialized_generic_index_record){
                                                                           *type,
                                                                           TC_NIRK_Struct,
                                                                           nth
                                                                       });


                        const TC_typeid ret = (TC_typeid){
                            .ty = TC_TY_Struct,
                            .info.type_id = nth,
                            .full_typeuse_span = type->full_typeuse_span
                        };

                        {
                            size_t_LIST in_progress_structs = size_t_LIST_new();
                            size_t_LIST in_progress_enums = size_t_LIST_new();

                            path_size_align_for_materialized_type(&ret, nct, &in_progress_structs, &in_progress_enums,
                                                                  target_info, source_info);

                            size_t_LIST_free(&in_progress_structs);
                            size_t_LIST_free(&in_progress_enums);
                        }


                        return ret;
                    }
                }
            }

            UNREACHABLE()
        }
    }
    UNREACHABLE()
}

typedef struct {
    TC_typeid type;
    SpannedIdentifier name;
} TC_arg;

init_prefixed_list_type(TC_arg)

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

typedef struct {
    size_t block;
    size_t instr_nth;
} TC_InstrPos;

init_prefixed_list_type(TC_InstrPos)

typedef enum {
    TC_VK_Int,
    TC_VK_Float,
    TC_VK_Bool,
    TC_VK_Array,
} TC_Value_kind;

typedef struct TC_Value_LIST TC_Value_LIST;

typedef struct TC_Value {
    TC_Value_kind kind;

    union {
        int64_t Int;
        float64_t Float;
        bool Bool;
        TC_Value_LIST* Array;
    };
} TC_Value;

init_prefixed_list_type(TC_Value)


typedef struct {
    /// false if not set
    bool known_temporal;

    TC_typeid type;

    /// will have parallel array with values
    bool is_value_known;
    // is set on the linear pass
    bool is_addressable;
    // union {
    //     void* null;
    //     TC_Value value;
    // };
} TC_Entry;

init_prefixed_list_type(TC_Entry)


static TC_structdef compiler_builtin__list_tc_structdef;
static TC_structdef compiler_builtin__slice_tc_structdef;


void init__compiler_builtin__tc_types(khash_t(sv_u64_map)* restrict identifier_map,
                                      uint64_t* restrict identifier_map_cnt, const TargetInfo* restrict target_info) {
    {
        const SV list_name = SV_from_str_len("List", 4);
        const SV list_len = SV_from_str_len("len", 3);
        const SV sv_uint32 = SV_from_str_len("Uint32", 6);

        TC_field_LIST list_fields = TC_field_LIST_new_with_capacity(1);
        TC_field_LIST_push(&list_fields, (TC_field){
                               .name = (SpannedIdentifier){
                                   .identifier = SV_to_identifier(identifier_map, identifier_map_cnt, &list_len),
                                   .span = (TokenSpan){.sv = list_len}
                               },
                               .type = (TC_typeid){
                                   .ty = TC_TY_Primitive,
                                   .info.primitive = TC_Primitive__Uint32,
                                   .full_typeuse_span = (TokenSpan){.sv = sv_uint32}
                               }
                           });

        // { ptr; len; cap; }
        // only len is exposed
        uint32_t* list_field_offsets = malloc(sizeof(uint32_t));
        *list_field_offsets = target_info->ptr_size;

        compiler_builtin__list_tc_structdef = (TC_structdef){
            .name = (SpannedIdentifier){
                .identifier = SV_to_identifier(identifier_map, identifier_map_cnt, &list_name),
                .span = (TokenSpan){.sv = list_name},
            },
            .fields = list_fields,
            .layout = (TC_struct_layout){
                .size = target_info->ptr_size * 3,
                .align = target_info->ptr_size,
                .field_offsets = list_field_offsets
            }
        };
    }


    {
        const SV slice_name = SV_from_str_len("Slice", 5);
        const SV slice_len = SV_from_str_len("len", 3);
        const SV sv_uint32 = SV_from_str_len("Uint32", 6);

        TC_field_LIST slice_fields = TC_field_LIST_new_with_capacity(1);
        TC_field_LIST_push(&slice_fields, (TC_field){
                               .name = (SpannedIdentifier){
                                   .identifier = SV_to_identifier(identifier_map, identifier_map_cnt, &slice_len),
                                   .span = (TokenSpan){.sv = slice_len}
                               },
                               .type = (TC_typeid){
                                   .ty = TC_TY_Primitive,
                                   .info.primitive = TC_Primitive__Uint32,
                                   .full_typeuse_span = (TokenSpan){.sv = sv_uint32}
                               }
                           });

        // { ptr; len; }
        // only len is exposed
        uint32_t* list_field_offsets = malloc(sizeof(uint32_t));
        *list_field_offsets = target_info->ptr_size;

        compiler_builtin__slice_tc_structdef = (TC_structdef){
            .name = (SpannedIdentifier){
                .identifier = SV_to_identifier(identifier_map, identifier_map_cnt, &slice_name),
                .span = (TokenSpan){.sv = slice_name}
            },
            .fields = slice_fields,
            .layout = (TC_struct_layout){
                .size = target_info->ptr_size * 2,
                .align = target_info->ptr_size,
                .field_offsets = list_field_offsets
            }
        };
    }
}


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


void type_inference_over_match_meta(const Pattern p, TC_Entry_LIST* restrict temporals_map, const TC_typeid* target_t,
                                    const TC_typeid* restrict unwrapped_target_t, const TC_TypeTable* restrict nct,
                                    const SourceInfo* restrict source_info) {
    switch (p.kind) {
        case PAT_LITERAL_INT:
        case PAT_LITERAL_FLOAT:
        case PAT_WILDCARD:
            break;
        case PAT_BINDING: {
            temporals_map->array[p.info.binding.temp.val] = (TC_Entry){
                .known_temporal = true,
                .type = *target_t,
                .is_value_known = false,
                .is_addressable = false,
            };
            break;
        }
        case PAT_VARIANT: {
            if (unwrapped_target_t->ty != TC_TY_Enum) break;

            const TC_enumdef edef = nct->enums.array[unwrapped_target_t->info.type_id];
            if (edef.name.identifier != p.info.variant.enum_name.identifier) break;
            bool found = false;
            size_t ix = 0;
            for (size_t k = 0; k < edef.variants.len; k++) {
                if (edef.variants.array[k].name.identifier == p.info.variant.variant_name.identifier) {
                    found = true;
                    ix = k;
                    break;
                }
            }
            if (!found) {
                const SV msg = SV_from_str_len("unknown enum variant", 20);
                type_error(&source_info->source, &source_info->source_filename, &p.info.variant.variant_name.span.sv,
                           &msg);
                exit(1);
            }

            if (p.info.variant.sub_patterns.len != edef.variants.array[ix].values_len) {
                const size_t MAX_SIZE = 1024;
                char* r_msg = malloc(sizeof(char) * MAX_SIZE);
                int len = snprintf(r_msg, MAX_SIZE, "expected %lu value arguments but found %lu",
                                   edef.variants.array[ix].values_len, p.info.variant.sub_patterns.len);
                const SV msg = SV_from_str_len(r_msg, len);
                type_error(&source_info->source, &source_info->source_filename, &p.info.variant.variant_name.span.sv,
                           &msg);
                exit(1);
            }

            for (size_t n = 0; n < p.info.variant.sub_patterns.len; n++) {
                TC_typeid* ut = &edef.variants.array[ix].values[n];
                while (ut->ty == TC_TY_Ref || ut->ty == TC_TY_Box) ut = ut->info.contained;
                const TC_typeid uwt = (TC_typeid) {
                    .ty = TC_TY_Ref,
                    .info.contained = ut
                };
                TC_typeid* target_inner = malloc(sizeof(TC_typeid));
                *target_inner = edef.variants.array[ix].values[n];
                const TC_typeid target = (TC_typeid) {
                    .ty = TC_TY_Ref,
                    .info.contained = target_inner
                };
                type_inference_over_match_meta(
                    p.info.variant.sub_patterns.array[n],
                    temporals_map,
                    &target,
                    &uwt,
                    nct,
                    source_info
                );
            }

            // WARN("UNIMPLEMENTED: PAT_VARIANT: in IR_INST_TY__MATCH_META__MATCH in type_inference")
            break;
        }
    }
}


void type_inference_pass_over_fn(
    const IR_func fn,
    khash_t(sv_u64_map)* restrict identifier_map,
    const IR_func_LIST* restrict functions,
    const CFFI_IR_extern_fn_LIST* restrict external_functions,
    TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info,
    TC_Entry_LIST* restrict temporals_map
) {
    const IR_instr_error_info_LIST* error_info_list = &fn.error_info_list;

    const IR_typeuse ir_ret_t = fn.fn_ret_t;
    const TC_typeid ret_t = resolve_tc_type(&ir_ret_t, nct, target_info, source_info);


    TC_arg_LIST args = TC_arg_LIST_new_with_capacity(fn.args.len);
    args.len = fn.args.len;
    for (size_t n = 0; n < fn.args.len; n++) {
        const TC_typeid type = resolve_tc_type(&fn.args.array[n].type, nct, target_info, source_info);
        args.array[n] = (TC_arg){
            type,
            fn.args.array[n].name
        };
    }

    // now we have args and return with real types, we now can check against that

    TC_InstrPos_LIST visited_phis = TC_InstrPos_LIST_new();


    /// `is_value_known` is always false in this pass
    *temporals_map = TC_Entry_LIST_new_with_capacity(fn.highest_temporal_number + 1);

    TC_Entry_LIST_push(temporals_map, (TC_Entry){.known_temporal = false});
    for (size_t n = 0; n < args.len; n++) {
        // printf("ARG %%%lu {", n+1);
        // print_TC_typeid(&args.array[n].type, identifier_map, nct);
        // printf("}\n");
        TC_Entry_LIST_push(temporals_map, (TC_Entry){
                               .known_temporal = true,
                               .type = args.array[n].type,
                               .is_addressable = false
                           });
    }

    while (temporals_map->len < fn.highest_temporal_number + 1) {
        TC_Entry_LIST_push(temporals_map, (TC_Entry){.known_temporal = false});
    }


    // Pre-scan: build a set of temporals that are REF_OF destinations.
    // These should not have their types overwritten by backward propagation from MOVE,
    // because REF_OF must always produce Ref<T>, not the inner type.
    bool* ref_of_dests = calloc(fn.highest_temporal_number + 1, sizeof(bool));
    for (ptrdiff_t block_id = 0; block_id < (ptrdiff_t) fn.blocks.len; block_id++) {
        const IR_block block = fn.blocks.array[block_id];
        for (size_t instr_nth = 0; instr_nth < block.instructions.len; instr_nth++) {
            const IR_instr instr = block.instructions.array[instr_nth];
            if (instr.ty == IR_INST_TY__REF_OF) {
                ref_of_dests[instr.info.ref_of.dest.val] = true;
            }
        }
    }

    // reverse instr walk + jump back on PHI to type-resolve

    for (ptrdiff_t block_id = (ptrdiff_t) fn.blocks.len - 1; block_id >= 0; block_id--) {
        const IR_block block = fn.blocks.array[block_id];
        // printf("BLOCK %ld:\n", block_id);
        bool perf_jump = false;
        for (size_t instr_nth = block.instructions.len; instr_nth-- > 0;) {
            if (perf_jump) {
                break;
            }
            const IR_instr instr = block.instructions.array[instr_nth];
            const IR_instr_error_info error_info = error_info_list->array[instr.error_info_index];

            // printf("  READ_INSTR: ");
            // print_IR_instr(&instr, identifier_map);
            // printf("\n");


            // ---------------------------------------------------------
            //              TYPE INFERENCE PASS
            // ---------------------------------------------------------

            // TODO: REMAKE TO A GENTLE AND FORGIVING PASS -> do not throw errors and try to match as much as possible

            switch (instr.ty) {
                case IR_INST_TY__INVALID: break;
                case IR_INST_TY__JUMP: break;
                case IR_INST_TY__RETURN_VOID: break;

                case IR_INST_TY__ASSIGN: {
                    if (!temporals_map->array[instr.info.assign.dest.val].known_temporal) break;

                    const TC_typeid dest_t = temporals_map->array[instr.info.assign.dest.val].type;

                    temporals_map->array[instr.info.assign.old.val] = (TC_Entry){
                        .known_temporal = true,
                        .type = dest_t,
                        .is_addressable = false,
                        .is_value_known = false,
                    };

                    const TC_typeid new_t = dest_t.ty == TC_TY_Ref ? *dest_t.info.contained : dest_t;

                    temporals_map->array[instr.info.assign.new.val] = (TC_Entry){
                        .known_temporal = true,
                        .type = new_t,
                        .is_addressable = false,
                        .is_value_known = false,
                    };
                }

                case IR_INST_TY__CONST_INT: {
                    // set as INT32 if unknown temporal and make the pass repeat
                    if (!temporals_map->array[instr.info.const_int.dest.val].known_temporal) {
                        temporals_map->array[instr.info.const_int.dest.val] = (TC_Entry){
                            .known_temporal = true,
                            .type = (TC_typeid){
                                .ty = TC_TY_Primitive,
                                .info.primitive = TC_Primitive__Int32,
                                .full_typeuse_span = (TokenSpan){.sv = SV_from_str_len("Int32", 5)}
                            },
                            .is_value_known = false,
                            .is_addressable = false
                        };
                        block_id = (ptrdiff_t) fn.blocks.len; // no -1, because of (block_id--) in the for loop
                        perf_jump = true;
                    }
                    break;
                }
                case IR_INST_TY__CONST_FLOAT: {
                    // set as FLOAT32 if unknown temporal and make the pass repeat
                    if (!temporals_map->array[instr.info.const_int.dest.val].known_temporal) {
                        temporals_map->array[instr.info.const_int.dest.val] = (TC_Entry){
                            .known_temporal = true,
                            .type = (TC_typeid){
                                .ty = TC_TY_Primitive,
                                .info.primitive = TC_Primitive__Float32,
                                .full_typeuse_span = (TokenSpan){.sv = SV_from_str_len("Float32", 7)}
                            },
                            .is_value_known = false,
                            .is_addressable = false
                        };
                        block_id = (ptrdiff_t) fn.blocks.len; // no -1, because of (block_id--) in the for loop
                        perf_jump = true;
                    }
                    break;
                }
                case IR_INST_TY__CONST_STRING: {
                    if (!temporals_map->array[instr.info.const_int.dest.val].known_temporal) {
                        TC_typeid* inner = malloc(sizeof(TC_typeid));
                        *inner = (TC_typeid){
                            .ty = TC_TY_Primitive,
                            .info.primitive = TC_Primitive__Char,
                            .full_typeuse_span = (TokenSpan){.sv = SV_from_str_len("Char", 4)}
                        };
                        temporals_map->array[instr.info.const_int.dest.val] = (TC_Entry){
                            .known_temporal = true,
                            .type = (TC_typeid){
                                .ty = TC_TY_Slice,
                                .info.contained = inner,
                                .full_typeuse_span = (TokenSpan){.sv = SV_from_str_len("Slice<Char>", 11)}
                            },
                            .is_value_known = false,
                            .is_addressable = false
                        };
                        block_id = (ptrdiff_t) fn.blocks.len; // no -1, because of (block_id--) in the for loop
                        perf_jump = true;
                    }
                    break;
                }
                case IR_INST_TY__VAR_DECL: {
                    // should be known by now from DROP
                    const TC_typeid t = resolve_tc_type(instr.info.var_decl.type, nct, target_info, source_info);
                    if (!temporals_map->array[instr.info.var_decl.dest.val].known_temporal) {
                        WARN("VAR_DECL with declare unknown temporal")
                    }

                    temporals_map->array[instr.info.var_decl.dest.val] = (TC_Entry){
                        true,
                        t,
                        false,
                        true,
                    };

                    break;
                }
                case IR_INST_TY__DROP: {
                    if (temporals_map->array[instr.info.drop.dest.val].known_temporal) break;

                    const TC_typeid type = resolve_tc_type(instr.info.drop.type, nct, target_info, source_info);

                    temporals_map->array[instr.info.drop.dest.val] = (TC_Entry){
                        .known_temporal = true,
                        .type = type,
                        .is_value_known = false,
                        .is_addressable = false
                    };
                    break;
                }
                case IR_INST_TY__MOVE: {
                    if (!temporals_map->array[instr.info.move.dest.val].known_temporal) break;

                    const TC_typeid dest_type = temporals_map->array[instr.info.move.dest.val].type;

                    if (!temporals_map->array[instr.info.move.src.val].known_temporal) {
                        // Do not propagate type backward if the src is a REF_OF destination.
                        // REF_OF must always produce Ref<T>, not the inner type from the dest.
                        if (ref_of_dests[instr.info.move.src.val]) {
                            break;
                        }
                        temporals_map->array[instr.info.move.src.val] = (TC_Entry){
                            .known_temporal = true,
                            .type = dest_type,
                            .is_value_known = false,
                            .is_addressable = false
                        };
                    }

                    break;
                }
                case IR_INST_TY__REF_OF: {
                    if (!temporals_map->array[instr.info.ref_of.src.val].known_temporal) {
                        break;
                    }
                    if (temporals_map->array[instr.info.ref_of.dest.val].known_temporal) {
                        break;
                    }

                    const TC_typeid src_t = temporals_map->array[instr.info.ref_of.src.val].type;

                    TC_typeid* out = malloc(sizeof(TC_typeid));
                    *out = src_t;

                    const size_t MAX_SIZE = 128;
                    char* new_type_name = malloc(sizeof(char) * MAX_SIZE);
                    const int out_len = snprintf(new_type_name, MAX_SIZE, "Ref<"SV_format">",
                                                 SV_v_args(src_t.full_typeuse_span.sv));
                    new_type_name = realloc(new_type_name, sizeof(char) * out_len);
                    const SV new_type = SV_from_str_len(new_type_name, out_len);

                    temporals_map->array[instr.info.ref_of.dest.val] = (TC_Entry){
                        .known_temporal = true,
                        .type = (TC_typeid){
                            .ty = TC_TY_Ref,
                            .info.contained = out,
                            .full_typeuse_span = (TokenSpan){.sv = new_type},
                        },
                        .is_value_known = false,
                        .is_addressable = false
                    };

                    block_id = (ptrdiff_t) fn.blocks.len; // no -1, because of (block_id--) in the for loop
                    perf_jump = true;

                    break;
                }
                case IR_INST_TY__OP_ADD:
                case IR_INST_TY__OP_SUB:
                case IR_INST_TY__OP_DIV:
                case IR_INST_TY__OP_MUL:
                case IR_INST_TY__OP_POW: {
                    const TC_Entry dest = temporals_map->array[instr.info.math_cmp_boolean_op.dest.val];
                    if (!dest.known_temporal) break;

                    if (!temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].known_temporal) {
                        temporals_map->array[instr.info.math_cmp_boolean_op.src1.val] = (TC_Entry){
                            .known_temporal = true,
                            .type = dest.type,
                            .is_value_known = false,
                            .is_addressable = false
                        };
                    }

                    if (!temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].known_temporal) {
                        temporals_map->array[instr.info.math_cmp_boolean_op.src2.val] = (TC_Entry){
                            .known_temporal = true,
                            .type = dest.type,
                            .is_value_known = false,
                            .is_addressable = false
                        };
                    }

                    break;
                }
                case IR_INST_TY__RETURN: {
                    if (!temporals_map->array[instr.info.return_value.val].known_temporal) {
                        temporals_map->array[instr.info.return_value.val] = (TC_Entry){
                            .known_temporal = true,
                            .type = ret_t,
                            .is_value_known = false,
                            .is_addressable = false
                        };
                    }

                    break;
                }
                case IR_INST_TY__OP_OR:
                case IR_INST_TY__OP_AND: {
                    // TODO: what to do here? Do i need <bool> <op> <bool> ???
                    if (!temporals_map->array[instr.info.math_cmp_boolean_op.dest.val].known_temporal) {
                        temporals_map->array[instr.info.math_cmp_boolean_op.dest.val] = (TC_Entry){
                            .known_temporal = true,
                            .type = (TC_typeid){
                                .ty = TC_TY_Primitive,
                                .info.primitive = TC_Primitive__Bool,
                                .full_typeuse_span = (TokenSpan){.sv = SV_from_str_len("Bool", 4)},
                            },
                            .is_value_known = false,
                            .is_addressable = false
                        };
                    }

                    if (!temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].known_temporal) {
                        temporals_map->array[instr.info.math_cmp_boolean_op.src1.val] = (TC_Entry){
                            .known_temporal = true,
                            .type = (TC_typeid){
                                .ty = TC_TY_Primitive,
                                .info.primitive = TC_Primitive__Bool,
                                .full_typeuse_span = (TokenSpan){.sv = SV_from_str_len("Bool", 4)},
                            },
                            .is_value_known = false,
                            .is_addressable = false
                        };
                    }

                    if (!temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].known_temporal) {
                        temporals_map->array[instr.info.math_cmp_boolean_op.src2.val] = (TC_Entry){
                            .known_temporal = true,
                            .type = (TC_typeid){
                                .ty = TC_TY_Primitive,
                                .info.primitive = TC_Primitive__Bool,
                                .full_typeuse_span = (TokenSpan){.sv = SV_from_str_len("Bool", 4)},
                            },
                            .is_value_known = false,
                            .is_addressable = false
                        };
                    }
                    break;
                }
                case IR_INST_TY__OP_CMP_EQ:
                case IR_INST_TY__OP_CMP_NOT_EQ:
                case IR_INST_TY__OP_CMP_LESS:
                case IR_INST_TY__OP_CMP_LESS_EQ:
                case IR_INST_TY__OP_CMP_GREATER:
                case IR_INST_TY__OP_CMP_GREATER_EQ: {
                    if (!temporals_map->array[instr.info.math_cmp_boolean_op.dest.val].known_temporal) {
                        temporals_map->array[instr.info.math_cmp_boolean_op.dest.val] = (TC_Entry){
                            .known_temporal = true,
                            .type = (TC_typeid){
                                .ty = TC_TY_Primitive,
                                .info.primitive = TC_Primitive__Bool,
                                .full_typeuse_span = (TokenSpan){.sv = SV_from_str_len("Bool", 4)},
                            },
                            .is_value_known = false,
                            .is_addressable = false
                        };
                    }

                    break;
                }
                case IR_INST_TY__PHI: {
                    bool visited = false;
                    for (size_t n = 0; n < visited_phis.len; n++) {
                        if ((ptrdiff_t) (visited_phis.array[n].block) == block_id && visited_phis.array[n].instr_nth ==
                            instr_nth) {
                            visited = true;
                            break;
                        }
                    }
                    if (visited) {
                        break;
                    }

                    const IR_temporal_block_phi_record_LIST* phi_args = instr.info.phi.args;
                    const TC_Entry dest = temporals_map->array[instr.info.phi.dest.val];
                    if (!dest.known_temporal) {
                        // printf("  PHI skipped, unknown dest, probably filled later by PHI\n");
                        break;
                    }

                    ptrdiff_t highest_block = block_id;
                    for (size_t n = 0; n < phi_args->len; n++) {
                        const IR_temporal_block_phi_record arg = phi_args->array[n];
                        if (!temporals_map->array[arg.temp.val].known_temporal) {
                            temporals_map->array[arg.temp.val] = (TC_Entry){
                                .known_temporal = true,
                                .type = dest.type,
                                .is_value_known = false,
                                .is_addressable = false
                            };
                        }
                        if ((ptrdiff_t) (arg.block) > highest_block) {
                            highest_block = (ptrdiff_t) arg.block;
                        }
                    }

                    TC_InstrPos_LIST_push(&visited_phis, (TC_InstrPos){.block = block_id, .instr_nth = instr_nth});

                    if (highest_block > block_id) {
                        // printf("block jumping to Block<%ld> (from %ld)\n", highest_block, block_id);
                        block_id = highest_block + 1; // because of (block_id--) in the for loop
                        perf_jump = true;
                    }

                    break;
                }
                case IR_INST_TY__MAKE_ARRAY: {
                    if (!temporals_map->array[instr.info.make_array.dest.val].known_temporal) {
                        // todo: maybe do inference based on items in array
                        break;
                    }
                    const TC_typeid dest_type = temporals_map->array[instr.info.make_array.dest.val].type;
                    // get inner array type
                    TC_typeid inner = dest_type;
                    while (inner.ty == TC_TY_Ref || inner.ty == TC_TY_Box) inner = *inner.info.contained;
                    if (inner.ty == TC_TY_Array || inner.ty == TC_TY_List || inner.ty == TC_TY_Slice) {
                        if (inner.ty == TC_TY_Array) {
                            inner = *inner.info.t_array.contained;
                        } else {
                            inner = *inner.info.contained;
                        }
                        for (size_t n = 0; n < instr.info.make_array.items->len; n++) {
                            const IR_temporal item = instr.info.make_array.items->array[n];
                            if (!temporals_map->array[item.val].known_temporal) {
                                temporals_map->array[item.val] = (TC_Entry){
                                    .known_temporal = true,
                                    .type = inner,
                                    .is_value_known = false,
                                    .is_addressable = false
                                };
                            }
                        }
                    }
                    break;
                }
                case IR_INST_TY__BRANCH: {
                    if (!temporals_map->array[instr.info.branch.cond.val].known_temporal) {
                        temporals_map->array[instr.info.branch.cond.val] = (TC_Entry){
                            .known_temporal = true,
                            .type = (TC_typeid){
                                .ty = TC_TY_Primitive,
                                .info.primitive = TC_Primitive__Bool,
                                .full_typeuse_span = (TokenSpan){.sv = SV_from_str_len("Bool", 4)},
                            },
                            .is_value_known = false,
                            .is_addressable = false
                        };
                    }
                    break;
                }
                case IR_INST_TY__GET_STRUCT_FIELD_PTR: {
                    if (temporals_map->array[instr.info.get_struct_field_ptr.struct_ptr.val].known_temporal &&
                        !temporals_map->array[instr.info.get_struct_field_ptr.dest.val].known_temporal
                    ) {
                        const TC_typeid src_struct = temporals_map->array[instr.info.get_struct_field_ptr.struct_ptr.
                            val].type;
                        TC_typeid inner_src = src_struct;
                        while (inner_src.ty == TC_TY_Ref || inner_src.ty == TC_TY_Box) {
                            inner_src = *inner_src.info.contained;
                        }
                        if (inner_src.ty == TC_TY_Struct) {
                            const TC_structdef* s = &nct->structs.array[inner_src.info.type_id];
                            for (size_t n = 0; n < s->fields.len; n++) {
                                if (s->fields.array[n].name.identifier == instr.info.get_struct_field_ptr.field_name.
                                    identifier) {
                                    TC_typeid* ref_inner = malloc(sizeof(TC_typeid));
                                    *ref_inner = s->fields.array[n].type;

                                    const size_t MAX_SIZE = 128;
                                    char* new_type_name = malloc(sizeof(char) * MAX_SIZE);
                                    const int out_len = snprintf(new_type_name, MAX_SIZE, "Ref<"SV_format">",
                                                                 SV_v_args(ref_inner->full_typeuse_span.sv));
                                    new_type_name = realloc(new_type_name, sizeof(char) * out_len);
                                    const SV new_type = SV_from_str_len(new_type_name, out_len);


                                    temporals_map->array[instr.info.get_struct_field_ptr.dest.val] = (TC_Entry){
                                        .known_temporal = true,
                                        .type = (TC_typeid){
                                            .ty = TC_TY_Ref,
                                            .info.contained = ref_inner,
                                            .full_typeuse_span = (TokenSpan){.sv = new_type},
                                            //ref_inner->full_typeuse_span
                                        },
                                        .is_value_known = false,
                                        .is_addressable = false
                                    };
                                    break;
                                }
                            }
                        } else if (inner_src.ty == TC_TY_List) {
                            const SV len = SV_from_str_len("len", 3);
                            if (instr.info.get_struct_field_ptr.field_name.identifier == SV_to_identifier(identifier_map, NULL, &len)) {
                                TC_typeid* ref_inner = malloc(sizeof(TC_typeid));
                                *ref_inner = compiler_builtin__list_tc_structdef.fields.array[0].type;

                                const size_t MAX_SIZE = 128;
                                char* new_type_name = malloc(sizeof(char) * MAX_SIZE);
                                const int out_len = snprintf(new_type_name, MAX_SIZE, "Ref<"SV_format">",
                                                             SV_v_args(ref_inner->full_typeuse_span.sv));
                                new_type_name = realloc(new_type_name, sizeof(char) * out_len);
                                const SV new_type = SV_from_str_len(new_type_name, out_len);


                                temporals_map->array[instr.info.get_struct_field_ptr.dest.val] = (TC_Entry){
                                    .known_temporal = true,
                                    .type = (TC_typeid){
                                        .ty = TC_TY_Ref,
                                        .info.contained = ref_inner,
                                        .full_typeuse_span = (TokenSpan){.sv = new_type},
                                    },
                                    .is_value_known = false,
                                    .is_addressable = false
                                };
                                block_id = (ptrdiff_t) fn.blocks.len;
                                perf_jump = true;
                            }

                        } else if (inner_src.ty == TC_TY_Slice) {
                            const SV len = SV_from_str_len("len", 3);
                            if (instr.info.get_struct_field_ptr.field_name.identifier == SV_to_identifier(identifier_map, NULL, &len)) {
                                TC_typeid* ref_inner = malloc(sizeof(TC_typeid));
                                *ref_inner = compiler_builtin__slice_tc_structdef.fields.array[0].type;

                                const size_t MAX_SIZE = 128;
                                char* new_type_name = malloc(sizeof(char) * MAX_SIZE);
                                const int out_len = snprintf(new_type_name, MAX_SIZE, "Ref<"SV_format">",
                                                             SV_v_args(ref_inner->full_typeuse_span.sv));
                                new_type_name = realloc(new_type_name, sizeof(char) * out_len);
                                const SV new_type = SV_from_str_len(new_type_name, out_len);


                                temporals_map->array[instr.info.get_struct_field_ptr.dest.val] = (TC_Entry){
                                    .known_temporal = true,
                                    .type = (TC_typeid){
                                        .ty = TC_TY_Ref,
                                        .info.contained = ref_inner,
                                        .full_typeuse_span = (TokenSpan){.sv = new_type},
                                    },
                                    .is_value_known = false,
                                    .is_addressable = false
                                };
                                block_id = (ptrdiff_t) fn.blocks.len;
                                perf_jump = true;
                            }
                        } else if (inner_src.ty == TC_TY_Array) {
                            // return const_int from type as LEN
                            // inner_src.info.t_array.array_size;
                            if (instr_nth + 1 < block.instructions.len && block.instructions.array[instr_nth + 1].ty == IR_INST_TY__READ_PTR && block.instructions.array[instr_nth + 1].info.read_ptr.ptr.val == instr.info.get_struct_field_ptr.dest.val) {
                                const IR_instr_error_info ei = fn.error_info_list.array[instr.error_info_index];
                                block.instructions.array[instr_nth] = (IR_instr) {
                                    .ty = IR_INST_TY__CONST_INT,
                                    .info.const_int = {
                                        .dest = instr.info.get_struct_field_ptr.dest,
                                        .val = inner_src.info.t_array.array_size
                                    },
                                    .error_info_index = instr.error_info_index
                                };
                                fn.error_info_list.array[instr.error_info_index] = (IR_instr_error_info) {
                                    .kind = IR_IEI_literal,
                                    .info.literal = ei.info.get_struct_field.whole
                                };
                                // ----------
                                const IR_instr nx = block.instructions.array[instr_nth+1];

                                block.instructions.array[instr_nth+1] = (IR_instr) {
                                    .ty = IR_INST_TY__MOVE,
                                    .info.move = {
                                        .dest = nx.info.read_ptr.dest,
                                        .src = nx.info.read_ptr.ptr
                                    },
                                    .error_info_index = nx.error_info_index
                                };
                                fn.error_info_list.array[nx.error_info_index] = (IR_instr_error_info) {
                                    .kind = IR_IEI_move,
                                    .info.move = {.value = ei.info.get_struct_field.whole }
                                };
                                block_id = (ptrdiff_t) fn.blocks.len;
                                perf_jump = true;
                            } else if (instr_nth + 1 < block.instructions.len && block.instructions.array[instr_nth + 1].ty == IR_INST_TY__STORE_PTR && block.instructions.array[instr_nth + 1].info.store_ptr.ptr.val == instr.info.get_struct_field_ptr.dest.val) {
                                INTERNAL_ERROR("type error here"); // cannot overrwrite array size
                            }
                        }
                    }
                    break;
                }
                case IR_INST_TY__READ_PTR: {
                    if (temporals_map->array[instr.info.read_ptr.dest.val].known_temporal &&
                        !temporals_map->array[instr.info.read_ptr.ptr.val].known_temporal
                    ) {
                        const TC_typeid dest = temporals_map->array[instr.info.read_ptr.dest.val].type;
                        TC_typeid* out = malloc(sizeof(TC_typeid));
                        *out = dest;

                        const size_t MAX_SIZE = 128;
                        char* new_type_name = malloc(sizeof(char) * MAX_SIZE);
                        const int out_len = snprintf(new_type_name, MAX_SIZE, "Ref<"SV_format">",
                                                     SV_v_args(dest.full_typeuse_span.sv));
                        new_type_name = realloc(new_type_name, sizeof(char) * out_len);
                        const SV new_type = SV_from_str_len(new_type_name, out_len);

                        temporals_map->array[instr.info.read_ptr.ptr.val] = (TC_Entry){
                            .known_temporal = true,
                            .type = (TC_typeid){
                                .ty = TC_TY_Ref,
                                .info.contained = out,
                                .full_typeuse_span = (TokenSpan){.sv = new_type}, //ref_inner->full_typeuse_span
                            },
                            .is_value_known = false,
                            .is_addressable = false
                        };
                        block_id = (ptrdiff_t) fn.blocks.len;
                        perf_jump = true;
                    } else if (temporals_map->array[instr.info.read_ptr.ptr.val].known_temporal &&
                               !temporals_map->array[instr.info.read_ptr.dest.val].known_temporal
                    ) {
                        TC_typeid inner = temporals_map->array[instr.info.read_ptr.ptr.val].type;
                        // size_t unwrap_cnt = 0;
                        if (inner.ty == TC_TY_Ref || inner.ty == TC_TY_Box) {
                            inner = *inner.info.contained;
                            temporals_map->array[instr.info.read_ptr.dest.val] = (TC_Entry){
                                .known_temporal = true,
                                .type = inner,
                                .is_value_known = false,
                                .is_addressable = true
                            };
                        }
                        block_id = (ptrdiff_t) fn.blocks.len;
                        perf_jump = true;
                        // while (inner.ty == TC_TY_Ref || inner.ty == TC_TY_Box) {
                        //     unwrap_cnt++;
                        //     inner = *inner.info.contained;
                        // }
                        // if (unwrap_cnt > 0) {
                        //     temporals_map->array[instr.info.read_ptr.dest.val] = (TC_Entry){
                        //         .known_temporal = true,
                        //         .type = inner,
                        //         .is_value_known = false,
                        //         .is_addressable = false
                        //     };
                        // }
                    }
                    break;
                }
                case IR_INST_TY__MAKE_STRUCT: {
                    // const TC_typeid type = resolve_tc_type(instr.info.make_struct.type,nct,target_info,source_info);
                    // if (type.ty != TC_TY_Struct) break;
                    //
                    // const TC_structdef* st = &nct->structs.array[type.info.type_id];
                    // // if (!temporals_map->array[instr.info.make_struct.dest.val].known_temporal) {
                    // temporals_map->array[instr.info.make_struct.dest.val] = (TC_Entry){
                    //     .known_temporal = true,
                    //     .type = type,
                    //     .is_value_known = false,
                    //     .is_addressable = false
                    // };
                    // }
                    TC_typeid type;
                    TC_Entry dest_entry = temporals_map->array[instr.info.make_struct.dest.val];

                    // 1. BIDIRECTIONAL INFERENCE: If the context (LHS) already knows the exact type, use it!
                    if (dest_entry.known_temporal && dest_entry.type.ty == TC_TY_Struct) {
                        type = dest_entry.type;
                    } else {
                        type = resolve_tc_type(instr.info.make_struct.type, nct, target_info, source_info);
                    }

                    if (type.ty != TC_TY_Struct) break;

                    // Anchor the destination temporal
                    temporals_map->array[instr.info.make_struct.dest.val] = (TC_Entry){
                        .known_temporal = true,
                        .type = type,
                        .is_value_known = false,
                        .is_addressable = false
                    };

                    const TC_structdef* st = &nct->structs.array[type.info.type_id];

                    for (size_t n = 0; n < instr.info.make_struct.args->len; n++) {
                        const IR_named_value arg = instr.info.make_struct.args->array[n];
                        // if (temporals_map->array[arg.value.val].known_temporal) continue;
                        for (size_t k = 0; k < st->fields.len; k++) {
                            if (st->fields.array[k].name.identifier == arg.name.identifier) {
                                temporals_map->array[arg.value.val] = (TC_Entry){
                                    .known_temporal = true,
                                    .type = st->fields.array[k].type,
                                    .is_value_known = false,
                                    .is_addressable = false
                                };
                                break;
                            }
                        }
                    }
                    break;
                }
                case IR_INST_TY__FN_CALL_RETVOID:
                case IR_INST_TY__FN_CALL_RETVAL: {
                    // check args
                    const IR_temporal_LIST* lk = instr.ty == IR_INST_TY__FN_CALL_RETVAL
                                                     ? instr.info.fn_call_retval.args
                                                     : instr.info.fn_call_retvoid.args;
                    const Identifier name = instr.ty == IR_INST_TY__FN_CALL_RETVAL
                                                ? instr.info.fn_call_retval.name.identifier
                                                : instr.info.fn_call_retvoid.name.identifier;

                    for (size_t n = 0; n < functions->len; n++) {
                        if (functions->array[n].fn_name.identifier == name) {
                            if (instr.ty == IR_INST_TY__FN_CALL_RETVAL) {
                                const TC_typeid t = resolve_tc_type(&functions->array[n].fn_ret_t, nct, target_info,
                                                                    source_info);
                                temporals_map->array[instr.info.fn_call_retval.dest.val] = (TC_Entry){
                                    .known_temporal = true,
                                    .type = t,
                                    .is_value_known = false,
                                    .is_addressable = false
                                };
                            }
                            if (lk->len == functions->array[n].args.len) {
                                for (size_t k = 0; k < lk->len; k++) {
                                    if (!temporals_map->array[lk->array[k].val].known_temporal) {
                                        const TC_typeid type = resolve_tc_type(
                                            &functions->array[n].args.array[k].type, nct, target_info, source_info);
                                        temporals_map->array[lk->array[k].val] = (TC_Entry){
                                            .known_temporal = true,
                                            .type = type,
                                            .is_value_known = false,
                                            .is_addressable = false
                                        };
                                    }
                                }
                            }
                        }
                    }
                    break;
                }
                case IR_INST_TY__INDEX_PTR: {
                    if (!temporals_map->array[instr.info.index_ptr.dest.val].known_temporal &&
                        temporals_map->array[instr.info.index_ptr.array_ptr.val].known_temporal
                    ) {
                        // dest unknown, array known
                        // const TC_typeid array_ptr = temporals_map->array[instr.info.index_ptr.array_ptr.val].type;
                        TC_typeid* inner_type = &temporals_map->array[instr.info.index_ptr.array_ptr.val].type;
                        bool run = true;
                        bool unpacked_array = false;
                        while (run) {
                            switch (inner_type->ty) {
                                case TC_TY_Box:
                                case TC_TY_Ref: {
                                    inner_type = inner_type->info.contained;
                                    break;
                                }
                                case TC_TY_List:
                                case TC_TY_Slice: {
                                    inner_type = inner_type->info.contained;
                                    unpacked_array = true;
                                    run = false;
                                    break;
                                }
                                case TC_TY_Array: {
                                    inner_type = inner_type->info.t_array.contained;
                                    unpacked_array = true;
                                    run = false;
                                    break;
                                }
                                default: {
                                    run = false;
                                    break;
                                }
                            }
                        }
                        if (unpacked_array) {
                            const size_t MAX_SIZE = 256;
                            char* t_sv = malloc(sizeof(char) * MAX_SIZE);
                            const SV inner_t_sv = format_TC_typeid(inner_type, identifier_map, nct);
                            const int t_len = snprintf(t_sv, MAX_SIZE, "Ref<"SV_format">", SV_v_args(inner_t_sv));

                            temporals_map->array[instr.info.index_ptr.dest.val] = (TC_Entry){
                                .known_temporal = true,
                                .type = (TC_typeid){
                                    .ty = TC_TY_Ref,
                                    .info.contained = inner_type,
                                    .full_typeuse_span = (TokenSpan){.sv = (SV){.start = t_sv, .len = t_len}}
                                },
                                .is_value_known = false,
                                .is_addressable = false
                            };
                        }
                    }
                    if (!temporals_map->array[instr.info.index_ptr.index_val.val].known_temporal) {
                        temporals_map->array[instr.info.index_ptr.index_val.val] = (TC_Entry){
                            .known_temporal = true,
                            .type = (TC_typeid){
                                .ty = TC_TY_Primitive,
                                .info.primitive = TC_Primitive__Uint32,
                                .full_typeuse_span = (TokenSpan){.sv = SV_from_str_len("Uint32", 6)}
                            },
                            .is_value_known = false,
                            .is_addressable = false
                        };
                    }
                    break;
                }
                // TODO: this
                case IR_INST_TY__STORE_PTR: {
                    // instr.info.store_ptr.ptr
                    // instr.info.store_ptr.value
                    if (temporals_map->array[instr.info.store_ptr.ptr.val].known_temporal &&
                        !temporals_map->array[instr.info.store_ptr.value.val].known_temporal
                    ) {
                        // ptr known, value not
                        const TC_typeid ptr_type = temporals_map->array[instr.info.store_ptr.ptr.val].type;
                        TC_typeid inner_type = ptr_type;
                        bool run = true;
                        bool unpacked = false;
                        while (run) {
                            switch (inner_type.ty) {
                                case TC_TY_Box:
                                case TC_TY_Ref: {
                                    inner_type = *inner_type.info.contained;
                                    unpacked = true;
                                    break;
                                }
                                default: {
                                    run = false;
                                }
                            }
                        }
                        if (unpacked) {
                            temporals_map->array[instr.info.store_ptr.value.val] = (TC_Entry){
                                .known_temporal = true,
                                .type = inner_type,
                                .is_value_known = false,
                                .is_addressable = false
                            };
                        }
                    }

                    if (temporals_map->array[instr.info.store_ptr.value.val].known_temporal &&
                        !temporals_map->array[instr.info.store_ptr.ptr.val].known_temporal
                    ) {
                        // value known, ptr unknown
                        // assume ptr should be Ref<T>
                        // conclusion: should be not needed in reality
                    }
                    break;
                }
                case IR_INST_TY__EXPLICIT_CAST: {
                    // EXPLICIT CAST HAS TYPE PRIORITY
                    // if (!temporals_map->array[instr.info.explicit_cast.dest.val].known_temporal) {
                    const TC_typeid t = resolve_tc_type(instr.info.explicit_cast.type, nct, target_info, source_info);
                    temporals_map->array[instr.info.explicit_cast.dest.val] = (TC_Entry){
                        .known_temporal = true,
                        .type = t,
                        .is_value_known = false,
                        .is_addressable = false
                    };
                    // }
                    break;
                }
                case IR_INST_TY__MAKE_ENUM: {
                    // const TC_typeid type = resolve_tc_type(instr.info.make_enum.type,nct,target_info,source_info);
                    // if (type.ty != TC_TY_Enum) break;
                    //
                    //
                    // // BE FORCEFULL, THIS IS TRUE ALWAYS
                    // // if (!temporals_map->array[instr.info.make_enum.dest.val].known_temporal) {
                    //     temporals_map->array[instr.info.make_enum.dest.val] = (TC_Entry){
                    //         .known_temporal = true,
                    //         .type = type,
                    //         .is_value_known = false,
                    //         .is_addressable = false
                    //     };
                    // // }
                    TC_typeid type;
                    TC_Entry dest_entry = temporals_map->array[instr.info.make_enum.dest.val];

                    // 1. BIDIRECTIONAL INFERENCE: If the context (LHS) already knows the exact type, use it!
                    if (dest_entry.known_temporal && dest_entry.type.ty == TC_TY_Enum) {
                        type = dest_entry.type;
                    } else {
                        type = resolve_tc_type(instr.info.make_enum.type, nct, target_info, source_info);
                    }

                    if (type.ty != TC_TY_Enum) break;

                    // Anchor the destination temporal
                    temporals_map->array[instr.info.make_enum.dest.val] = (TC_Entry){
                        .known_temporal = true,
                        .type = type,
                        .is_value_known = false,
                        .is_addressable = false
                    };

                    const TC_enumdef* en = &nct->enums.array[type.info.type_id];


                    // const TC_enumdef* en = &nct->enums.array[type.info.type_id];
                    for (size_t v = 0; v < en->variants.len; v++) {
                        if (en->variants.array[v].name.identifier == instr.info.make_enum.variant && en->variants.array[
                                v].values_len == instr.info.make_enum.args->len) {
                            for (size_t a = 0; a < instr.info.make_enum.args->len; a++) {
                                if (!temporals_map->array[instr.info.make_enum.args->array[a].val].known_temporal) {
                                    temporals_map->array[instr.info.make_enum.args->array[a].val] = (TC_Entry){
                                        .known_temporal = true,
                                        .type = en->variants.array[v].values[a],
                                        .is_value_known = false,
                                        .is_addressable = false
                                    };
                                }
                            }
                            break;
                        }
                    }
                    break;
                }
                case IR_INST_TY__MATCH_META__TARGET: {
                    // nothing
                    break;
                }
                case IR_INST_TY__MATCH_META__MATCH: {
                    if (!temporals_map->array[instr.info.match_meta__match.target.val].known_temporal) break;
                    const TC_typeid target_t = temporals_map->array[instr.info.match_meta__match.target.val].type;
                    TC_typeid unwrapped_target_t = target_t;
                    while (unwrapped_target_t.ty == TC_TY_Ref || unwrapped_target_t.ty == TC_TY_Box)
                        unwrapped_target_t = *unwrapped_target_t.info.contained;

                    // propagate type to pattern binds
                    for (size_t n = 0; n < instr.info.match_meta__match.patterns.len; n++) {
                        const Pattern p = instr.info.match_meta__match.patterns.array[n];
                        type_inference_over_match_meta(p, temporals_map, &target_t, &unwrapped_target_t, nct,
                                                       source_info);
                    }

                    break;
                }
                case ENUM_GET_FIELD_PTR: {
                    if (!temporals_map->array[instr.info.enum_get_field_ptr.enum_src.val].known_temporal) break;
                    const TC_typeid src_t = temporals_map->array[instr.info.enum_get_field_ptr.enum_src.val].type;
                    TC_typeid inner_t = src_t;
                    while (inner_t.ty == TC_TY_Ref || inner_t.ty == TC_TY_Box) inner_t = *inner_t.info.contained;
                    if (inner_t.ty != TC_TY_Enum) break;
                    if (instr.info.enum_get_field_ptr.enum_name.identifier != nct->enums.array[inner_t.info.type_id].
                        name.identifier)
                        break;

                    const TC_enumdef edef = nct->enums.array[inner_t.info.type_id];
                    for (size_t n = 0; n < edef.variants.len; n++) {
                        if (edef.variants.array[n].name.identifier == instr.info.enum_get_field_ptr.variant_name.
                            identifier) {
                            if (edef.variants.array[n].values_len > instr.info.enum_get_field_ptr.value_nth) {
                                TC_typeid* inner = malloc(sizeof(TC_typeid));
                                *inner = edef.variants.array[n].values[instr.info.enum_get_field_ptr.value_nth];
                                temporals_map->array[instr.info.enum_get_field_ptr.dest.val] = (TC_Entry){
                                    .known_temporal = true,
                                    .type = (TC_typeid) {
                                        .ty = TC_TY_Ref,
                                        .info.contained = inner,
                                    },
                                    .is_value_known = false,
                                    .is_addressable = false
                                };
                            }
                        }
                    }


                    break;
                }
                case IR_INST_TY__ENUM_VARIANT_NUMBER_EXTRACT: {
                    if (!temporals_map->array[instr.info.enum_variant_number_extract.enum_src.val].known_temporal)
                        break
                                ;

                    TC_typeid enum_t = temporals_map->array[instr.info.enum_variant_number_extract.enum_src.val].type;

                    while (enum_t.ty == TC_TY_Ref || enum_t.ty == TC_TY_Box) enum_t = *enum_t.info.contained;

                    if (enum_t.ty != TC_TY_Enum) {
                        temporals_map->array[instr.info.enum_variant_number_extract.dest.val] = (TC_Entry){
                            .known_temporal = true,
                            .type = (TC_typeid){
                                .ty = TC_TY_Primitive,
                                .info.primitive = TC_Primitive__Uint32,
                                .full_typeuse_span = (TokenSpan){.sv = SV_from_str_len("Uint32", 6)}
                            },
                            .is_value_known = false,
                            .is_addressable = false,
                        };
                        break;
                    }

                    TC_Primitive_TY prim;
                    SV prim_sv;
                    switch (nct->enums.array[enum_t.info.type_id].discriminant_size) {
                        case 1: {
                            prim = TC_Primitive__Uint8;
                            prim_sv = SV_from_str_len("Uint8", 5);
                            break;
                        }
                        case 2: {
                            prim = TC_Primitive__Uint16;
                            prim_sv = SV_from_str_len("Uint16", 6);
                            break;
                        }
                        case 4: {
                            prim = TC_Primitive__Uint32;
                            prim_sv = SV_from_str_len("Uint32", 6);
                            break;
                        }
                        case 8: {
                            prim = TC_Primitive__Uint64;
                            prim_sv = SV_from_str_len("Uint64", 6);
                            break;
                        }
                        default: INTERNAL_ERROR()
                    }

                    temporals_map->array[instr.info.enum_variant_number_extract.dest.val] = (TC_Entry){
                        .known_temporal = true,
                        .type = (TC_typeid){
                            .ty = TC_TY_Primitive,
                            .info.primitive = prim,
                            .full_typeuse_span = (TokenSpan){.sv = prim_sv}
                        },
                        .is_value_known = false,
                        .is_addressable = false,
                    };
                    break;


                    break;
                }
                case IR_INST_TY__ENUM_VARIANT_NUMBER_FROM_TYPE: {
                    // TODO: replace this instr with CONST_INT??
                    // instr.info.enum_variant_number_from_type.enum_name.identifier
                    bool found = false;
                    uint32_t nth = 0;
                    // for (size_t n = 0; n < nct->name_index.len; n++) {
                    //     if (
                    //         nct->name_index.array[n].name == instr.info.enum_variant_number_from_type.enum_name.identifier
                    //         &&
                    //         nct->name_index.array[n].kind == TC_NIRK_Enum
                    //     ) {
                    //         nth = nct->name_index.array[n].nth;
                    //         found = true;
                    //         break;
                    //     }
                    // }
                    for (size_t n = 0; n < nct->enums.len; n++) {
                        if (nct->enums.array[n].name.identifier == instr.info.enum_variant_number_from_type.enum_name.
                            identifier) {
                            // nth = nct->name_index.array[n].nth;
                            nth = n;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        // type_error()
                        UNIMPLEMENTED("error here");
                        exit(1);
                    }

                    const TC_enumdef edef = nct->enums.array[nth];
                    uint32_t variant_n = 0;
                    bool variant_n_found = false;
                    for (size_t n = 0; n < edef.variants.len; n++) {
                        if (edef.variants.array[n].name.identifier == instr.info.enum_variant_number_from_type.
                            variant_name.identifier) {
                            variant_n = n;
                            variant_n_found = true;
                            break;
                        }
                    }
                    if (!variant_n_found) {
                        UNIMPLEMENTED("error here");
                        exit(1);
                    }

                    block.instructions.array[instr_nth] = (IR_instr){
                        .ty = IR_INST_TY__CONST_INT,
                        .info.const_int = {
                            .dest = instr.info.enum_variant_number_from_type.dest,
                            .val = variant_n
                        }
                    };
                    instr_nth = instr_nth + 1; // whould it work??
                    // printf("todo: IR_INST_TY__ENUM_VARIANT_NUMBER_FROM_TYPE | type inference pass\n");
                    break;
                }
            }
        }
    }
    // todo: fix external fn calls (do not infer types for them)
    free(ref_of_dests);
}

typedef struct {
    IR_temporal temporal;
    TC_typeid type;
} IR_temporal_and_TC_type;

// while Ref<T>, unpack
IR_temporal_and_TC_type perf_auto_deref(const TC_typeid* type, const IR_temporal* intake, size_t* highest_temporal,
                                        TC_Entry_LIST* temporals_map) {
    if (type->ty != TC_TY_Ref) {
        return (IR_temporal_and_TC_type){.temporal = *intake, .type = *type};
    }
    TC_typeid t = *type;
    uint64_t te = intake->val;
    while (t.ty == TC_TY_Ref) {
        const uint64_t new_temporal = (*highest_temporal)++;
        printf("   %%%lu = READ_PTR %%%lu\n", new_temporal, te);
        te = new_temporal;
        t = *t.info.contained;
        if (new_temporal + 1 > temporals_map->cap) {
            TC_Entry_LIST_inflate(temporals_map, new_temporal + 5);
        }
        temporals_map->array[new_temporal] = (TC_Entry){
            .type = t,
            .known_temporal = true,
            .is_value_known = false,
            .is_addressable = false,
        };
    }
    return (IR_temporal_and_TC_type){.temporal = (IR_temporal){.val = te}, .type = t};
}

typedef struct {
    IR_temporal a;
    IR_temporal b;
} DoubleIR_temporal;

typedef struct {
    IR_temporal a;
    IR_temporal b;
    TC_typeid t;
} DoubleIR_temporal_and_TC_typeid;

/// returns numeric type that fits both
TC_typeid get_common_numeric_type(const TC_typeid t1, const TC_typeid t2) {
    if (t1.ty != TC_TY_Primitive) {
        INTERNAL_ERROR("should be primitive at this point")
    }
    if (t2.ty != TC_TY_Primitive) {
        INTERNAL_ERROR("should be primitive at this point")
    }

    /*
    RULE: Uint<X> to Uint<Y>, Int<X> to Int<Y>, Float<X> to Float<Y>, Uint<X> | Int<X> to Char/Bool
     */

    const TC_Primitive_TY p1 = t1.info.primitive;
    const TC_Primitive_TY p2 = t2.info.primitive;

    if (p1 == p2) return t1;

    if (p1 == TC_Primitive__Float32 && p2 == TC_Primitive__Float64) return t2;
    if (p1 == TC_Primitive__Float64 && p2 == TC_Primitive__Float32) return t1;

    switch (p1) {
        case TC_Primitive__Uint64: {
            switch (p2) {
                case TC_Primitive__Int8:
                case TC_Primitive__Int16:
                case TC_Primitive__Int32:
                case TC_Primitive__Int64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Uint8:
                case TC_Primitive__Uint16:
                case TC_Primitive__Uint32: {
                    return t1;
                }
                case TC_Primitive__Uint64: {
                    INTERNAL_ERROR();
                    break;
                }
                case TC_Primitive__Float32:
                case TC_Primitive__Float64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Char:
                case TC_Primitive__Bool: {
                    return t1;
                }
            }
            INTERNAL_ERROR("control flow")
        }
        case TC_Primitive__Uint32: {
            switch (p2) {
                case TC_Primitive__Int8:
                case TC_Primitive__Int16:
                case TC_Primitive__Int32:
                case TC_Primitive__Int64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Uint8:
                case TC_Primitive__Uint16: {
                    return t1;
                }
                case TC_Primitive__Uint32: {
                    INTERNAL_ERROR()
                    break;
                }
                case TC_Primitive__Uint64: {
                    return t2;
                }
                case TC_Primitive__Float32:
                case TC_Primitive__Float64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Char:
                case TC_Primitive__Bool: {
                    return t1;
                }
            }
            INTERNAL_ERROR("control flow")
        }
        case TC_Primitive__Uint16: {
            switch (p2) {
                case TC_Primitive__Int8:
                case TC_Primitive__Int16:
                case TC_Primitive__Int32:
                case TC_Primitive__Int64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Uint8: {
                    return t1;
                }
                case TC_Primitive__Uint16: {
                    INTERNAL_ERROR()
                    break;
                }
                case TC_Primitive__Uint32:
                case TC_Primitive__Uint64: {
                    return t2;
                }
                case TC_Primitive__Float32:
                case TC_Primitive__Float64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Char:
                case TC_Primitive__Bool: {
                    return t1;
                }
            }
            INTERNAL_ERROR("control flow")
        }
        case TC_Primitive__Uint8: {
            switch (p2) {
                case TC_Primitive__Int8:
                case TC_Primitive__Int16:
                case TC_Primitive__Int32:
                case TC_Primitive__Int64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Uint8: {
                    INTERNAL_ERROR()
                    break;
                }
                case TC_Primitive__Uint16:
                case TC_Primitive__Uint32:
                case TC_Primitive__Uint64: {
                    return t2;
                }
                case TC_Primitive__Float32:
                case TC_Primitive__Float64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Char:
                case TC_Primitive__Bool: {
                    return t1;
                }
            }
            INTERNAL_ERROR("control flow")
        }
        case TC_Primitive__Int64: {
            switch (p2) {
                case TC_Primitive__Int8:
                case TC_Primitive__Int16:
                case TC_Primitive__Int32: {
                    return t1;
                }
                case TC_Primitive__Int64: {
                    INTERNAL_ERROR()
                    break;
                }
                case TC_Primitive__Uint8:
                case TC_Primitive__Uint16:
                case TC_Primitive__Uint32:
                case TC_Primitive__Uint64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Float32:
                case TC_Primitive__Float64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Char:
                case TC_Primitive__Bool: {
                    return t1;
                }
            }
            INTERNAL_ERROR("control flow")
        }
        case TC_Primitive__Int32: {
            switch (p2) {
                case TC_Primitive__Int8:
                case TC_Primitive__Int16: {
                    return t1;
                }
                case TC_Primitive__Int32: {
                    INTERNAL_ERROR()
                    break;
                }
                case TC_Primitive__Int64: {
                    return t2;
                }
                case TC_Primitive__Uint8:
                case TC_Primitive__Uint16:
                case TC_Primitive__Uint32:
                case TC_Primitive__Uint64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Float32:
                case TC_Primitive__Float64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Char:
                case TC_Primitive__Bool: {
                    return t1;
                }
            }
            INTERNAL_ERROR("control flow")
        }
        case TC_Primitive__Int16: {
            switch (p2) {
                case TC_Primitive__Int8: {
                    return t1;
                }
                case TC_Primitive__Int16: {
                    INTERNAL_ERROR()
                    break;
                }
                case TC_Primitive__Int32:
                case TC_Primitive__Int64: {
                    return t2;
                }
                case TC_Primitive__Uint8:
                case TC_Primitive__Uint16:
                case TC_Primitive__Uint32:
                case TC_Primitive__Uint64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Float32:
                case TC_Primitive__Float64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Char:
                case TC_Primitive__Bool: {
                    return t1;
                }
            }
            INTERNAL_ERROR("control flow")
        }
        case TC_Primitive__Int8: {
            switch (p2) {
                case TC_Primitive__Int8: {
                    INTERNAL_ERROR()
                    break;
                }
                case TC_Primitive__Int16:
                case TC_Primitive__Int32:
                case TC_Primitive__Int64: {
                    return t2;
                }
                case TC_Primitive__Uint8:
                case TC_Primitive__Uint16:
                case TC_Primitive__Uint32:
                case TC_Primitive__Uint64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Float32:
                case TC_Primitive__Float64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Char:
                case TC_Primitive__Bool: {
                    return t1;
                }
            }
            INTERNAL_ERROR("control flow")
        }
        case TC_Primitive__Float32: {
            switch (p2) {
                case TC_Primitive__Int8:
                case TC_Primitive__Int16:
                case TC_Primitive__Int32:
                case TC_Primitive__Int64:
                case TC_Primitive__Uint8:
                case TC_Primitive__Uint16:
                case TC_Primitive__Uint32:
                case TC_Primitive__Uint64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Float32: {
                    INTERNAL_ERROR()
                    break;
                }
                case TC_Primitive__Float64: {
                    return t2;
                }
                case TC_Primitive__Char:
                case TC_Primitive__Bool: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
            }
            INTERNAL_ERROR("control flow")
        }
        case TC_Primitive__Float64: {
            switch (p2) {
                case TC_Primitive__Int8:
                case TC_Primitive__Int16:
                case TC_Primitive__Int32:
                case TC_Primitive__Int64:
                case TC_Primitive__Uint8:
                case TC_Primitive__Uint16:
                case TC_Primitive__Uint32:
                case TC_Primitive__Uint64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Float32: {
                    return t1;
                }
                case TC_Primitive__Float64: {
                    INTERNAL_ERROR()
                    break;
                }
                case TC_Primitive__Char:
                case TC_Primitive__Bool: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
            }
            INTERNAL_ERROR("control flow")
        }
        case TC_Primitive__Char:
        case TC_Primitive__Bool: {
            switch (p2) {
                case TC_Primitive__Int8:
                case TC_Primitive__Int16:
                case TC_Primitive__Int32:
                case TC_Primitive__Int64:
                case TC_Primitive__Uint8:
                case TC_Primitive__Uint16:
                case TC_Primitive__Uint32:
                case TC_Primitive__Uint64: {
                    return t2;
                }
                case TC_Primitive__Float32:
                case TC_Primitive__Float64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Char: {
                    return t2;
                }
                case TC_Primitive__Bool: {
                    return t1;
                }
            }
            INTERNAL_ERROR("control flow")
        }
    }

    // returns INVALID
    return (TC_typeid){.ty = TC_TY_Void};
}

/// returns TRUE when possible
/// describes intentional explicit casting by the user
bool can_be_explicit_casted(const TC_typeid* from, const TC_typeid* to) {
    if (from->ty == TC_TY_Primitive && to->ty == TC_TY_Primitive) {
        return true;
    }
    return false;
    UNIMPLEMENTED()
}


bool can_const_narrow(const uint64_t temporal, const TC_typeid* to, const TC_Entry_LIST* temporals_map,
                      const TC_Value_LIST* temporals_map_values) {
    const TC_Entry* entry = &temporals_map->array[temporal];

    // If the compiler doesn't know the exact value at compile time, we must reject it.
    if (!entry->is_value_known) return false;
    if (to->ty != TC_TY_Primitive) return false;

    if (temporal + 1 > temporals_map_values->cap) {
        return false;
    }
    if (temporals_map_values->array[temporal].kind != TC_VK_Int) {
        return false;
    }

    const int64_t val = temporals_map_values->array[temporal].Int;

    switch (to->info.primitive) {
        case TC_Primitive__Int8: return val >= -128 && val <= 127;
        case TC_Primitive__Uint8: return val >= 0 && val <= 255;
        case TC_Primitive__Int16: return val >= -32768 && val <= 32767;
        case TC_Primitive__Uint16: return val >= 0 && val <= 65535;
        case TC_Primitive__Int32: return val >= -2147483648LL && val <= 2147483647LL;
        case TC_Primitive__Uint32: return val >= 0 && val <= 4294967295LL;
        case TC_Primitive__Char: return val >= 0 && val <= 255; // Or 1114111 for Unicode
        // Int64/Uint64 are already caught by implicit cast or are the max size anyway
        default: return false;
    }
    INTERNAL_ERROR("control flow error")
}

/// returns TRUE when possible
/// describes unintentional casting originating from two types not being equal (like Int32 a = 5_i16)
/// for unsigned to signed, allows only if fits, e.g Uint8 into Int16
bool can_be_implicit_casted(const TC_typeid* from, const TC_typeid* to) {
    if (to->ty == TC_TY_Primitive && from->ty == TC_TY_Primitive) {
        if (to->info.primitive == from->info.primitive) return true;

        switch (from->info.primitive) {
            case TC_Primitive__Int8: {
                return to->info.primitive >= TC_Primitive__Int8 && to->info.primitive <= TC_Primitive__Int64;
            }
            case TC_Primitive__Int16: {
                return to->info.primitive >= TC_Primitive__Int16 && to->info.primitive <= TC_Primitive__Int64;
            }
            case TC_Primitive__Int32: {
                return to->info.primitive >= TC_Primitive__Int32 && to->info.primitive <= TC_Primitive__Int64;
            }
            case TC_Primitive__Int64: {
                return to->info.primitive == TC_Primitive__Int64;
            }
            case TC_Primitive__Uint8: {
                const bool is_valid_unsigned = (to->info.primitive >= TC_Primitive__Uint8 && to->info.primitive <=
                                                TC_Primitive__Uint64);
                const bool is_safe_signed = (to->info.primitive >= TC_Primitive__Int16 && to->info.primitive <=
                                             TC_Primitive__Int64);
                return is_valid_unsigned || is_safe_signed;
            }
            case TC_Primitive__Uint16: {
                const bool is_valid_unsigned = (to->info.primitive >= TC_Primitive__Uint16 && to->info.primitive <=
                                                TC_Primitive__Uint64);
                const bool is_safe_signed = (to->info.primitive >= TC_Primitive__Int32 && to->info.primitive <=
                                             TC_Primitive__Int64);
                return is_valid_unsigned || is_safe_signed;
            }
            case TC_Primitive__Uint32: {
                const bool is_valid_unsigned = (to->info.primitive >= TC_Primitive__Uint32 && to->info.primitive <=
                                                TC_Primitive__Uint64);
                const bool is_safe_signed = (to->info.primitive == TC_Primitive__Int64);
                return is_valid_unsigned || is_safe_signed;
            }
            case TC_Primitive__Uint64: {
                return to->info.primitive == TC_Primitive__Uint64;
            }
            case TC_Primitive__Float32: {
                return to->info.primitive == TC_Primitive__Float32 || to->info.primitive == TC_Primitive__Float64;
            }
            case TC_Primitive__Float64: {
                return to->info.primitive == TC_Primitive__Float64;
            }
            case TC_Primitive__Char:
            case TC_Primitive__Bool: {
                return
                        (to->info.primitive >= TC_Primitive__Int8 && to->info.primitive <= TC_Primitive__Int64) ||
                        (to->info.primitive >= TC_Primitive__Uint8 && to->info.primitive <= TC_Primitive__Uint64);
            }
        }
    }
    return false;
    UNIMPLEMENTED()
}

/// on type ERROR: returns t->void, a->is_error -> T(1)/F(0), b->is_error -> T(1)/F(0)
DoubleIR_temporal_and_TC_typeid perf_numeric_promotion(
    const IR_temporal* a,
    const TC_typeid* a_type,
    const IR_temporal* b,
    const TC_typeid* b_type,
    size_t* highest_temporal,
    bool* is_out_error,
    bool* is_out_error_incompatible_number_relation,
    TC_Entry_LIST* temporals_map,
    const TC_Value_LIST* temporals_map_values
) {
    const TC_typeid safe_a_type = *a_type;
    const TC_typeid safe_b_type = *b_type;


    const IR_temporal_and_TC_type derefered_a = perf_auto_deref(&safe_a_type, a, highest_temporal, temporals_map);
    const IR_temporal_and_TC_type derefered_b = perf_auto_deref(&safe_b_type, b, highest_temporal, temporals_map);

    {
        // check if derefered_a and derefered_b are number types
        bool is_error = false;
        uint64_t error_a = 0;
        uint64_t error_b = 0;

        if (derefered_a.type.ty != TC_TY_Primitive) {
            is_error = true;
            error_a = 1;
        } else {
            switch (derefered_a.type.info.primitive) {
                case TC_Primitive__Char:
                case TC_Primitive__Bool: {
                    is_error = true;
                    error_a = 1;
                    break;
                }
                default: {
                    break;
                }
            }
        }

        if (derefered_b.type.ty != TC_TY_Primitive) {
            is_error = true;
            error_b = 1;
        } else {
            switch (derefered_b.type.info.primitive) {
                case TC_Primitive__Char:
                case TC_Primitive__Bool: {
                    is_error = true;
                    error_b = 1;
                    break;
                }
                default: {
                    break;
                }
            }
        }

        if (is_error) {
            *is_out_error = true;
            *is_out_error_incompatible_number_relation = false;
            return (DoubleIR_temporal_and_TC_typeid){
                .a = (IR_temporal){.val = error_a},
                .b = (IR_temporal){.val = error_b},
                .t = (TC_typeid){.ty = TC_TY_Void}
            };
        }
    }

    TC_typeid target_op_type = {.ty = TC_TY_Void};

    // 1. CONSTANT PRIORITY RESOLUTION
    // Check if either side is a known compile-time value
    const bool a_known = temporals_map->array[derefered_a.temporal.val].is_value_known;
    const bool b_known = temporals_map->array[derefered_b.temporal.val].is_value_known;

    // If A is a variable and B is a constant that fits into A's type
    if (!a_known && b_known && can_const_narrow(derefered_b.temporal.val, &derefered_a.type, temporals_map,
                                                temporals_map_values)) {
        target_op_type = derefered_a.type;
    }
    // If B is a variable and A is a constant that fits into B's type
    else if (!b_known && a_known && can_const_narrow(derefered_a.temporal.val, &derefered_b.type, temporals_map,
                                                     temporals_map_values)) {
        target_op_type = derefered_b.type;
    }
    // 2. STANDARD PROMOTION
    else {
        target_op_type = get_common_numeric_type(derefered_a.type, derefered_b.type);

        // 3. FALLBACK: Mixed Signedness where get_common fails, but they are both constants
        if (target_op_type.ty == TC_TY_Void) {
            if (can_const_narrow(derefered_a.temporal.val, &derefered_b.type, temporals_map, temporals_map_values)) {
                target_op_type = derefered_b.type;
            } else if (can_const_narrow(derefered_b.temporal.val, &derefered_a.type, temporals_map,
                                        temporals_map_values)) {
                target_op_type = derefered_a.type;
            }
        }
    }

    if (target_op_type.ty == TC_TY_Void) {
        // incompatible integers -> between each other
        *is_out_error = true;
        *is_out_error_incompatible_number_relation = true;
        return (DoubleIR_temporal_and_TC_typeid){
            .a = (IR_temporal){.val = 0},
            .b = (IR_temporal){.val = 0},
            .t = target_op_type
        };
    }

    IR_temporal new_a = derefered_a.temporal;
    IR_temporal new_b = derefered_b.temporal;

    // CAST A
    if (derefered_a.type.info.primitive != target_op_type.info.primitive) {
        const uint64_t new_temporal = (*highest_temporal)++;

        if (a_known) {
            printf("   %%%lu = CAST<"SV_format">(%%%lu) [IMPLICIT CONST-NARROW]\n", new_temporal,
                   SV_v_args(target_op_type.full_typeuse_span.sv), new_a.val);
        } else {
            printf("   %%%lu = CAST<"SV_format">(%%%lu)\n", new_temporal,
                   SV_v_args(target_op_type.full_typeuse_span.sv), new_a.val);
        }
        new_a.val = new_temporal;

        // IMPORTANT: Register the new temporal so the pipeline doesn't break later!
        if (temporals_map->cap < new_temporal + 1) TC_Entry_LIST_inflate(temporals_map, new_temporal + 5);
        temporals_map->array[new_temporal] = temporals_map->array[derefered_a.temporal.val];
        temporals_map->array[new_temporal].type = target_op_type;
    }

    // CAST B
    if (derefered_b.type.info.primitive != target_op_type.info.primitive) {
        const uint64_t new_temporal = (*highest_temporal)++;

        if (b_known) {
            printf("   %%%lu = CAST<"SV_format">(%%%lu) [IMPLICIT CONST-NARROW]\n", new_temporal,
                   SV_v_args(target_op_type.full_typeuse_span.sv), new_b.val);
        } else {
            printf("   %%%lu = CAST<"SV_format">(%%%lu)\n", new_temporal,
                   SV_v_args(target_op_type.full_typeuse_span.sv), new_b.val);
        }
        new_b.val = new_temporal;

        // IMPORTANT: Register the new temporal
        if (temporals_map->cap < new_temporal + 1) TC_Entry_LIST_inflate(temporals_map, new_temporal + 1);
        temporals_map->array[new_temporal] = temporals_map->array[derefered_b.temporal.val];
        temporals_map->array[new_temporal].type = target_op_type;
    }

    *is_out_error = false;
    *is_out_error_incompatible_number_relation = false;
    return (DoubleIR_temporal_and_TC_typeid){
        .a = new_a,
        .b = new_b,
        .t = target_op_type
    };
}

// The Condition Coercer
IR_temporal perf_bool(const IR_temporal* intake, const TC_typeid* intake_type, size_t* highest_temporal,
                      TC_Entry_LIST* temporals_map) {
    const IR_temporal_and_TC_type derefd = perf_auto_deref(intake_type, intake, highest_temporal, temporals_map);
    if (derefd.type.ty == TC_TY_Primitive && derefd.type.info.primitive == TC_Primitive__Bool) {
        return derefd.temporal;
    }
    if (derefd.type.ty == TC_TY_Primitive) {
        // emit check not zero
        // OP_CMP_NOT_EQ
        const uint64_t new_temporal = (*highest_temporal)++;
        printf("   %%%lu = OP_CMP_NOT_EQ %%%lu, 0\n", new_temporal, derefd.temporal.val);

        if (new_temporal + 1 > temporals_map->cap) {
            TC_Entry_LIST_inflate(temporals_map, new_temporal + 5);
        }
        temporals_map->array[new_temporal] = (TC_Entry){
            .type = (TC_typeid){
                .ty = TC_TY_Primitive,
                .info.primitive = TC_Primitive__Bool,
                .full_typeuse_span = (TokenSpan){.sv = SV_from_str_len("Bool", 4)}
            },
            .known_temporal = true,
            .is_value_known = false,
            .is_addressable = false,
        };

        return (IR_temporal){.val = new_temporal};
    }

    INTERNAL_ERROR("typechecking error here....");
}


IR_temporal_and_TC_type perf_auto_ref(const IR_temporal* intake, const TC_typeid* intake_type, size_t* highest_temporal,
                                      TC_Entry_LIST* temporals_map) {
    const uint64_t new_temporal = (*highest_temporal)++;
    printf("   %%%lu = REF_OF %%%lu\n", new_temporal, intake->val);

    const TC_typeid nt = (TC_typeid){
        .ty = TC_TY_Ref,
        .info.contained = (TC_typeid *) intake_type,
        .full_typeuse_span = (TokenSpan){.sv = intake_type->full_typeuse_span.sv}
    };

    if (new_temporal + 1 > temporals_map->cap) {
        TC_Entry_LIST_inflate(temporals_map, new_temporal + 5);
    }
    temporals_map->array[new_temporal] = (TC_Entry){
        .type = nt,
        .known_temporal = true,
        .is_value_known = false,
        .is_addressable = false,
    };

    return (IR_temporal_and_TC_type){.temporal = (IR_temporal){.val = new_temporal}, .type = nt};
}

IR_temporal perf_autos(
    const IR_temporal* restrict intake,
    const TC_typeid* intake_type,
    const TC_typeid* output_type,
    size_t* restrict highest_temporal,
    const khash_t(sv_u64_map)* restrict identifier_map,
    const TC_TypeTable* nct,
    TC_Entry_LIST* restrict temporals_map,
    const SourceInfo* restrict source_info,
    const SV* value_token,
    TC_Value_LIST* temporals_map_values
) {
    if (is_type_eq(intake_type, output_type)) {
        return *intake;
    }

    uint64_t current_temp = intake->val;
    TC_typeid current_type = *intake_type;
    while (current_type.ty == TC_TY_Ref) {
        const uint64_t new_temporal = (*highest_temporal)++;
        printf("   %%%lu = READ_PTR %%%lu\n", new_temporal, current_temp);
        current_temp = new_temporal;
        current_type = *current_type.info.contained;
        if (temporals_map->cap < current_temp + 1) {
            TC_Entry_LIST_inflate(temporals_map, current_temp + 5);
        }
        temporals_map->array[current_temp] = (TC_Entry){
            .known_temporal = true,
            .type = current_type,
            .is_value_known = false,
            .is_addressable = true,
        };
    }

    if (is_type_eq(&current_type, output_type)) {
        return (IR_temporal){.val = current_temp};
    }

    const TC_typeid* target_base = (output_type->ty == TC_TY_Ref) ? (output_type->info.contained) : output_type;

    // Check standard widening OR constant narrowing
    bool can_widen = can_be_implicit_casted(&current_type, target_base);
    bool can_narrow = !can_widen && can_const_narrow(current_temp, target_base, temporals_map, temporals_map_values);

    // if (!is_type_eq(&current_type, target_base) && can_be_implicit_casted(&current_type, target_base)) {
    //     const uint64_t new_temp = (*highest_temporal)++;
    //     const SV target_base_sv = format_TC_typeid(target_base, identifier_map, nct);
    //     printf("   %%%lu = CAST<"SV_format">(%%%lu) [IMPLICIT]\n", new_temp, SV_v_args(target_base_sv), current_temp);
    //     current_type = *target_base;
    //     current_temp = new_temp;
    //
    //     if (temporals_map->cap < current_temp+1) {
    //         TC_Entry_LIST_inflate(temporals_map, current_temp+1);
    //     }
    //     temporals_map->array[current_temp] = (TC_Entry) {
    //         .known_temporal = true,
    //         .type = current_type,
    //         .is_value_known = false,
    //         .is_addressable = false,
    //     };
    // }
    if (!is_type_eq(&current_type, target_base) && (can_widen || can_narrow)) {
        const uint64_t new_temp = (*highest_temporal)++;
        const SV target_base_sv = format_TC_typeid(target_base, identifier_map, nct);

        // Print different debug info if it was a constant narrow
        if (can_narrow) {
            printf("   %%%lu = CAST<"SV_format">(%%%lu) [IMPLICIT CONST-NARROW]\n", new_temp, SV_v_args(target_base_sv),
                   current_temp);
        } else {
            printf("   %%%lu = CAST<"SV_format">(%%%lu) [IMPLICIT]\n", new_temp, SV_v_args(target_base_sv),
                   current_temp);
        }

        if (temporals_map->cap < new_temp + 1) {
            TC_Entry_LIST_inflate(temporals_map, new_temp + 1);
        }

        if (temporals_map_values->cap < new_temp + 1) {
            TC_Value_LIST_inflate(temporals_map_values, new_temp + 1);
        }

        // CRITICAL: Propagate the known value!
        TC_Entry old_entry = temporals_map->array[current_temp];
        temporals_map->array[new_temp] = (TC_Entry){
            .known_temporal = true,
            .type = *target_base,
            .is_value_known = old_entry.is_value_known,
            // && temporals_map_values->array[current_temp].kind == TC_VK_Int,
            .is_addressable = false,
        };

        // Copy the actual value over!
        // if (old_entry.is_value_known && temporals_map_values->array[current_temp].kind == TC_VK_Int) {
        //     temporals_map_values->array[new_temp] = (TC_Value) {
        //         .kind = TC_VK_Int,
        //         .Int = temporals_map_values->array[current_temp].Int
        //     };
        // }
        if (old_entry.is_value_known) {
            temporals_map_values->array[new_temp] = temporals_map_values->array[current_temp];
        }


        current_type = *target_base;
        current_temp = new_temp;
    }

    if (is_type_eq(&current_type, output_type)) {
        return (IR_temporal){.val = current_temp};
    }

    // auto-ref
    if (output_type->ty == TC_TY_Ref && is_type_eq(output_type->info.contained, &current_type)) {
        if (!temporals_map->array[current_temp].is_addressable) {
            const SV msg = SV_from_str_len("cannot take reference of temporary value", 40);
            type_error(&source_info->source, &source_info->source_filename, value_token, &msg);
            const SV hint = SV_from_str_len("consider creating variable with that value and than using it here", 0);
            hint_standalone(&hint);
            exit(1);
        }
        const uint64_t new_temporal = (*highest_temporal)++;
        printf("   %%%lu = REF_OF %%%lu\n", new_temporal, current_temp);
        current_type = *output_type;
        current_temp = new_temporal;

        if (temporals_map->cap < current_temp + 1) {
            TC_Entry_LIST_inflate(temporals_map, current_temp + 5);
        }
        temporals_map->array[current_temp] = (TC_Entry){
            .known_temporal = true,
            .type = current_type,
            .is_value_known = false,
            .is_addressable = false,
        };
    }

    if (is_type_eq(output_type, &current_type)) {
        return (IR_temporal){.val = current_temp};
    } else {
        // Type mismatch! Could not coerce...
        const SV in_type = format_TC_typeid(intake_type, identifier_map, nct);
        const SV out_type = format_TC_typeid(output_type, identifier_map, nct);
        const size_t MAX_SIZE = 1024;
        char* r_msg = malloc(sizeof(char) * MAX_SIZE);
        int len = snprintf(r_msg, MAX_SIZE,
                           "Type mismatch! Could not coerce "C_BOLD""SV_format""C_RESET" into "C_BOLD""SV_format""
                           C_RESET"", SV_v_args(in_type), SV_v_args(out_type));
        const SV msg = SV_from_str_len(r_msg, len);
        type_error(&source_info->source, &source_info->source_filename, value_token, &msg);
        exit(1);
    }
}


SizeAlign calc_size_of(const TC_typeid* type, const TC_TypeTable* restrict nct,
                       const TargetInfo* restrict target_info) {
    switch (type->ty) {
        case TC_TY_Void: INTERNAL_ERROR("SIZEOF_ VOID")
        case TC_TY_Primitive: {
            return (SizeAlign){TC_Primitive_TY_size(type->info.primitive), TC_Primitive_TY_size(type->info.primitive)};
        }
        case TC_TY_Slice: {
            return (SizeAlign){target_info->ptr_size * 2, target_info->ptr_size}; // { ptr; len; }
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
            return (SizeAlign){target_info->ptr_size * 3, target_info->ptr_size}; // { ptr; len; cap; }
        }
        case TC_TY_External_Type: INTERNAL_ERROR("SIZEOF_ EXTERNAL_TYPE")
    }
    UNREACHABLE()
}




/// returns true if found
void find_type_param_parallel(const IR_typeuse* restrict t1, const TC_typeid* restrict t2,
                              const SpannedIdentifier_LIST* restrict generic_args,
                              TC_GenericArgMap_LIST* restrict out) {
    if (t2->ty == TC_TY_Void) return; // used for function calls discarding return values
    switch (t1->tt_ty) {
        case IR_TY_Void: return;
        case IR_TY_Primitive: return;
        case IR_TY_Named: {
            for (size_t n = 0; n < generic_args->len; n++) {
                if (generic_args->array[n].identifier == t1->info.named.identifier) {
                    // t2->ty
                    bool found = false;
                    size_t found_nth = 0;
                    for (size_t k = 0; k < out->len; k++) {
                        if (out->array[k].generic_name.identifier == generic_args->array[n].identifier) {
                            found = true;
                            found_nth = k;
                            break;
                        }
                    }
                    if (!found) {
                        // add
                        TC_GenericArgMap_LIST_push(out, (TC_GenericArgMap){
                                                       .generic_name = generic_args->array[n],
                                                       .generic_name_t = *t2
                                                   });
                    } else {
                        // cmp -> type consistency
                        if (!is_type_eq(&out->array[found_nth].generic_name_t, t2)) {
                            INTERNAL_ERROR("type error here")
                        }
                    }
                }
            }

            break;
        }
        case IR_TY_Ref: {
            const TC_typeid* nx = t2->ty == TC_TY_Ref ? t2->info.contained : t2;
            // if (t2->ty != TC_TY_Ref) {
            //     INTERNAL_ERROR("type error here")
            // }
            find_type_param_parallel(t1->contained, nx, generic_args, out);
            break;
        }
        case IR_TY_Box: {
            if (t2->ty != TC_TY_Box) {
                INTERNAL_ERROR("type error here")
            }
            find_type_param_parallel(t1->contained, t2->info.contained, generic_args, out);
            break;
        }
        case IR_TY_Slice: {
            if (t2->ty != TC_TY_Slice) {
                INTERNAL_ERROR("type error here")
            }
            find_type_param_parallel(t1->contained, t2->info.contained, generic_args, out);
            break;
        }
        case IR_TY_Array: {
            if (t2->ty != TC_TY_Array) {
                INTERNAL_ERROR("type error here")
            }
            if (t1->array_const_size != t2->info.t_array.array_size) {
                INTERNAL_ERROR("type error here")
            }
            find_type_param_parallel(t1->contained, t2->info.t_array.contained, generic_args, out);
            break;
        }
        case IR_TY_List: {
            if (t2->ty != TC_TY_List) {
                INTERNAL_ERROR("type error here")
            }
            find_type_param_parallel(t1->contained, t2->info.contained, generic_args, out);
            break;
        }
        case IR_TY_Generic: {
            if (t2->ty != TC_TY_Enum && t2->ty != TC_TY_Struct) {
                INTERNAL_ERROR("type error here")
            }
            UNIMPLEMENTED()
            /*
            Ah, I see! Because TC_typeid for structs and enums uses an internal index (type_id) into
nct->structs or nct->enums, the direct nested type tree is flattened into that ID.

To solve this, you can use the compiler's materialized generics cache (nct->materialized_generics):

 1 When a generic type is materialized, the compiler stores a TC_materialized_generic_index_record
   which pairs the original concrete IR_typeuse (e.g., List<Int32> with its actual arguments) with
   its assigned nth index (type_id).
 2 Therefore, when t2 is a TC_TY_Struct or TC_TY_Enum, you can look up nct->materialized_generics
   using its type_id to retrieve the original concrete IR_typeuse arguments.
 3 Once you have those concrete IR_typeuse arguments, you can map them directly to your generic
   parameters!
             */
            // find_type_param_parallel(t1->contained, t2->info.contained, generic_args, out);
            break;
        }
    }
}

IR_typeuse typeuse_from_tc(const TC_typeid* tc, TC_TypeTable* restrict nct) {
    switch (tc->ty) {
        case TC_TY_Void: {
            return (IR_typeuse){
                .tt_ty = IR_TY_Void,
                .full_typeuse_span = tc->full_typeuse_span
            };
        }
        case TC_TY_Primitive: {
            return (IR_typeuse){
                .tt_ty = IR_TY_Primitive,
                .info.primitive = tc->info.primitive,
                .full_typeuse_span = tc->full_typeuse_span
            };
        }
        case TC_TY_Ref:
        case TC_TY_Box:
        case TC_TY_Slice:
        case TC_TY_List: {
            IR_typeuse* inner = malloc(sizeof(IR_typeuse));
            *inner = typeuse_from_tc(tc->info.contained, nct);

            const IR_typeuse_type tt_ty = (tc->ty == TC_TY_Ref)
                                              ? IR_TY_Ref
                                              : (tc->ty == TC_TY_Box)
                                                    ? IR_TY_Box
                                                    : (tc->ty == TC_TY_Slice)
                                                          ? IR_TY_Slice
                                                          : IR_TY_List;
            return (IR_typeuse){
                .tt_ty = tt_ty,
                .contained = inner,
                .contained_len = 1,
                .full_typeuse_span = tc->full_typeuse_span
            };
        }
        case TC_TY_Array: {
            IR_typeuse* inner = malloc(sizeof(IR_typeuse));
            *inner = typeuse_from_tc(tc->info.t_array.contained, nct);
            return (IR_typeuse){
                .tt_ty = IR_TY_Array,
                .contained = inner,
                .array_const_size = tc->info.t_array.array_size,
                .full_typeuse_span = tc->full_typeuse_span
            };
        }
        case TC_TY_Struct:
        case TC_TY_Enum: {
            // 1. Check if this struct/enum was a materialized generic
            for (size_t n = 0; n < nct->materialized_generics.len; n++) {
                const TC_materialized_generic_index_record mr = nct->materialized_generics.array[n];
                if (mr.nth == tc->info.type_id) {
                    if ((tc->ty == TC_TY_Struct && mr.kind == TC_NIRK_Struct) ||
                        (tc->ty == TC_TY_Enum && mr.kind == TC_NIRK_Enum)) {
                        return mr.generic; // Returns the original IR_typeuse with generic args!
                    }
                }
            }

            // 2. Otherwise, it's a regular non-generic struct/enum
            const SpannedIdentifier name = (tc->ty == TC_TY_Struct)
                                               ? nct->structs.array[tc->info.type_id].name
                                               : nct->enums.array[tc->info.type_id].name;

            return (IR_typeuse){
                .tt_ty = IR_TY_Named,
                .info.named = name,
                .full_typeuse_span = tc->full_typeuse_span
            };
        }
        case TC_TY_External_Type: {
            return (IR_typeuse){
                .tt_ty = IR_TY_Named,
                .info.named = tc->info.external_type,
                .full_typeuse_span = tc->full_typeuse_span
            };
        }
    }


    UNREACHABLE()
}


IR_typeuse materialization_fn_type_patch(const IR_typeuse* use, const TC_GenericArgMap_LIST* restrict map,
                                         TC_TypeTable* restrict nct) {
    switch (use->tt_ty) {
        case IR_TY_Void:
        case IR_TY_Primitive: {
            return *use;
        }
        case IR_TY_Named: {
            for (size_t n = 0; n < map->len; n++) {
                if (map->array[n].generic_name.identifier == use->info.named.identifier) {
                    return typeuse_from_tc(&map->array[n].generic_name_t, nct);
                }
            }
            return *use;
        }
        case IR_TY_Ref:
        case IR_TY_Box:
        case IR_TY_Slice:
        case IR_TY_List: {
            IR_typeuse* inner = malloc(sizeof(IR_typeuse));
            *inner = materialization_fn_type_patch(use->contained, map, nct);


            return (IR_typeuse){
                .tt_ty = use->tt_ty,
                .info.named = use->info.named,
                .contained = inner,
                .contained_len = 1,
                .full_typeuse_span = use->full_typeuse_span,
            };
        }
        case IR_TY_Array: {
            IR_typeuse* inner = malloc(sizeof(IR_typeuse));
            *inner = materialization_fn_type_patch(use->contained, map, nct);


            return (IR_typeuse){
                .tt_ty = use->tt_ty,
                .info.named = use->info.named,
                .contained = inner,
                // .contained_len = 1,
                .array_const_size = use->array_const_size,
                .full_typeuse_span = use->full_typeuse_span,
            };
        }

        case IR_TY_Generic: {
            UNIMPLEMENTED()
        }
    }
    UNREACHABLE()
}

const IR_func* materialize_function(const IR_generic_function* restrict gf, const TC_typeid_LIST found_args_t,
                                    const TC_typeid* found_ret_t, TC_TypeTable* restrict nct) {
    // find type args in functions args+ret -> in parallel, unwrap IR_typeuse and TC_typeid types
    TC_GenericArgMap_LIST map = TC_GenericArgMap_LIST_new_with_capacity(gf->generic_args.len);

    find_type_param_parallel(&gf->fn.fn_ret_t, found_ret_t, &gf->generic_args, &map);
    for (size_t n = 0; n < found_args_t.len; n++) {
        if (map.len == gf->generic_args.len) break;
        find_type_param_parallel(&gf->fn.args.array[n].type, &found_args_t.array[n], &gf->generic_args, &map);
    }

    if (map.len != gf->generic_args.len) {
        INTERNAL_ERROR("throw type error here | could not infer type ")
    }

    for (size_t n = 0; n < nct->materialized_generic_functions.len; n++) {
        const IR_materialized_generic_function* gd = &nct->materialized_generic_functions.array[n];
        if (gd->fn_name == gf->fn.fn_name.identifier) {
            bool is_valid = true;
            for (size_t k = 0; k < map.len; k++) {
                for (size_t j = 0; j < gd->map.len; j++) {
                    if (gd->map.array[j].generic_name.identifier == map.array[k].generic_name.identifier) {
                        if (!is_type_eq(&gd->map.array[j].generic_name_t, &map.array[k].generic_name_t)) {
                            is_valid = false;
                        }
                        break;
                    }
                }
                if (!is_valid) {
                    break;
                }
            }
            if (!is_valid) {
                continue;
            }
            return &gd->fn;
        }
    }

    // not found -> materialize

    IR_func out_fn = gf->fn;
    out_fn.blocks = IR_block_LIST_new_with_capacity(gf->fn.blocks.len);
    for (size_t block_n = 0; block_n < gf->fn.blocks.len; block_n++) {
        IR_block b = (IR_block){
            .instructions = IR_instr_LIST_new_with_capacity(gf->fn.blocks.array[block_n].instructions.len),
            .bindings = IR_SSA_binding_LIST_new_with_capacity(gf->fn.blocks.array[block_n].bindings.len)
        };
        for (size_t bb = 0; bb < gf->fn.blocks.array[block_n].bindings.len; bb++) {
            IR_SSA_binding_LIST_push(&b.bindings, gf->fn.blocks.array[block_n].bindings.array[bb]);
        }
        for (size_t ii = 0; ii < gf->fn.blocks.array[block_n].instructions.len; ii++) {
            IR_instr instr = gf->fn.blocks.array[block_n].instructions.array[ii];
            switch (instr.ty) {
                case IR_INST_TY__DROP: {
                    const IR_typeuse out = materialization_fn_type_patch(instr.info.drop.type, &map, nct);
                    instr.info.drop.type = malloc(sizeof(IR_typeuse));
                    *instr.info.drop.type = out;
                    break;
                }
                case IR_INST_TY__VAR_DECL: {
                    const IR_typeuse out = materialization_fn_type_patch(instr.info.var_decl.type, &map, nct);
                    instr.info.var_decl.type = malloc(sizeof(IR_typeuse));
                    *instr.info.var_decl.type = out;
                    break;
                }
                case IR_INST_TY__EXPLICIT_CAST: {
                    const IR_typeuse out = materialization_fn_type_patch(instr.info.explicit_cast.type, &map, nct);
                    instr.info.explicit_cast.type = malloc(sizeof(IR_typeuse));
                    *instr.info.explicit_cast.type = out;
                }
                default: {
                    break;
                }
            }
            IR_instr_LIST_push(&b.instructions, instr);
        }
        IR_block_LIST_push(&out_fn.blocks, b);
    }
    out_fn.error_info_list = IR_instr_error_info_LIST_new_with_capacity(gf->fn.error_info_list.len);
    for (size_t e = 0; e < gf->fn.error_info_list.len; e++) {
        IR_instr_error_info_LIST_push(&out_fn.error_info_list, gf->fn.error_info_list.array[e]);
    }

    const IR_typeuse patched_ret_t = materialization_fn_type_patch(&gf->fn.fn_ret_t, &map, nct);
    out_fn.fn_ret_t = patched_ret_t;

    out_fn.args = IR_typename_pair_LIST_new_with_capacity(gf->fn.args.len);
    for (size_t a = 0; a < gf->fn.args.len; a++) {
        const IR_typeuse patched_t = materialization_fn_type_patch(&gf->fn.args.array[a].type, &map, nct);
        IR_typename_pair_LIST_push(&out_fn.args, (IR_typename_pair){
                                       .type = patched_t,
                                       .name = gf->fn.args.array[a].name
                                   });
    }

    // create new name
    const size_t new_name_size = sizeof(char) * gf->fn.fn_name.span.sv.len + 20;
    char* new_name = malloc(new_name_size);
    int ns = snprintf(new_name, new_name_size, "FN_GT_%lu___"SV_format"", nct->materialized_generic_functions.len + 1,
                      SV_v_args(out_fn.fn_name.span.sv));
    out_fn.raw_name = SV_from_str_len(new_name, ns);

    IR_materialized_generic_function_LIST_push(&nct->materialized_generic_functions, (IR_materialized_generic_function){
                                                   .fn_name = out_fn.fn_name.identifier,
                                                   .fn = out_fn,
                                                   .map = map,
                                               });

    // printf("[INFO] materialized a generic function\n");
    return &nct->materialized_generic_functions.array[nct->materialized_generic_functions.len - 1].fn;
}

MIR_func lowering_and_emission_pass_over_fn(
    const IR_func fn,
    const khash_t(sv_u64_map)* restrict identifier_map,
    const IR_func_LIST* restrict functions,
    const CFFI_IR_extern_fn_LIST* restrict external_functions,
    TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info,
    TC_Entry_LIST* restrict temporals_map,
    const IR_temporal_LIST promote_to_stack_temps,
    TC_typeid_LIST* restrict type_needing_destructor
) {
    printf("LINEAR TYPECHECK AND LOWERING:\n");

    TC_Value_LIST temporals_map_values = TC_Value_LIST_new_with_capacity(fn.highest_temporal_number + 1);
    size_t highest_temporal = fn.highest_temporal_number + 1;

    MIR_block_LIST mir_blocks = MIR_block_LIST_new_with_capacity(fn.blocks.len);
    MIR_temporal_LIST mir_temporals = MIR_temporal_LIST_new_with_capacity(highest_temporal);

    for (ptrdiff_t block_id = 0; block_id < (ptrdiff_t) fn.blocks.len; block_id++) {
        const IR_block block = fn.blocks.array[block_id];
        MIR_instr_LIST mir_instrs = MIR_instr_LIST_new_with_capacity(block.instructions.len);

        printf("  BLOCK %lu:\n", block_id);
        for (size_t instr_nth = 0; instr_nth < block.instructions.len; instr_nth++) {
            const IR_instr instr = block.instructions.array[instr_nth];

            switch (instr.ty) {
                case IR_INST_TY__INVALID: {
                    INTERNAL_ERROR("IR_INST_TY__INVALID");
                    break;
                }

                case IR_INST_TY__ASSIGN: {
                    bool is_value = true;
                    for(size_t n = 0; n < promote_to_stack_temps.len; n++) {
                        if(
                            promote_to_stack_temps.array[n].val == instr.info.assign.old.val ||
                            promote_to_stack_temps.array[n].val == instr.info.assign.dest.val
                        ) {
                            is_value = false;
                            break;
                        }
                    }
                    if (is_value) {
                        printf("   %%%lu = MOVE %%%lu (ASSIGN)\n", instr.info.assign.dest.val, instr.info.assign.new.val);
                        printf("   DROP %%%lu (ASSIGN)\n", instr.info.assign.old.val);
                    } else {
                        printf("   STORE %%%lu, %%%lu (ASSIGN)\n", instr.info.assign.old.val, instr.info.assign.new.val);
                        printf("   %%%lu = MOVE %%%lu (ASSIGN)\n", instr.info.assign.dest.val, instr.info.assign.old.val);
                    }
//                     instr.info.assign.old
//                     UNIMPLEMENTED()
                    break;
                }

                case IR_INST_TY__PHI: {
                    // Typecheck: Ensure all incoming PHI branches are the exact same type
                    // instr.info.phi.dest.val is the source of truth
                    const TC_typeid dest_t = temporals_map->array[instr.info.phi.dest.val].type;

                    for (size_t n = 0; n < instr.info.phi.args->len; n++) {
                        const IR_temporal_block_phi_record* record = &instr.info.phi.args->array[n];

                        if (!is_type_eq(&dest_t, &temporals_map->array[record->temp.val].type)) {
                            INTERNAL_ERROR("PHI: should not happen because of backward inference pass");
                        }
                    }


                    printf("   %%%lu = PHI { ", instr.info.phi.dest.val);
                    for (size_t n = 0; n < instr.info.phi.args->len; n++) {
                        printf("<BLOCK %lu, %%%lu>", instr.info.phi.args->array[n].block,
                               instr.info.phi.args->array[n].temp.val);
                        if (n + 1 != instr.info.phi.args->len) printf(", ");
                    }
                    printf("}\n");
                    break;
                }
                case IR_INST_TY__DROP: {
                    const TC_typeid t = resolve_tc_type(instr.info.drop.type, nct, target_info, source_info);
                    const SV type_sv = format_TC_typeid(&t, identifier_map, nct);
                    printf("   DROP <"SV_format"> %%%lu ",SV_v_args(type_sv), instr.info.drop.dest.val);
                    if (t.ty == TC_TY_Primitive) {
                        printf("[STACK]\n");
                    } else if (t.ty == TC_TY_Ref || t.ty == TC_TY_Slice) {
                        printf("[STACK]\n");
                    } else {
                        printf("[STACK+RAII]\n");
                    }
                    break;
                }
                case IR_INST_TY__MOVE: {
                    // auto-deref, auto-ref, ...
                    const TC_typeid dest_t = temporals_map->array[instr.info.move.dest.val].type;
                    const TC_typeid src_t = temporals_map->array[instr.info.move.src.val].type;

                    const IR_temporal out = perf_autos(
                        &instr.info.move.src,
                        &src_t,
                        &dest_t,
                        &highest_temporal,
                        identifier_map,
                        nct,
                        temporals_map,
                        source_info,
                        &fn.error_info_list.array[instr.error_info_index].info.move.value.sv,
                        &temporals_map_values
                    );
                    printf("   %%%lu = MOVE %%%lu\n", instr.info.move.dest.val, out.val);

                    break;
                }
                case IR_INST_TY__REF_OF: {

                    const TC_typeid src_t = temporals_map->array[instr.info.ref_of.src.val].type;
                    const TC_typeid dest_t = temporals_map->array[instr.info.ref_of.dest.val].type;


                    if (instr_nth + 1 < block.instructions.len && block.instructions.array[instr_nth+1].ty == IR_INST_TY__MATCH_META__TARGET && src_t.ty == TC_TY_Ref) {
                        // emit MOVE
                        printf("   %%%lu = MOVE %%%lu\n", instr.info.ref_of.dest.val, instr.info.ref_of.src.val);
                        temporals_map->array[instr.info.ref_of.dest.val].type = src_t;
                        break;
                    }


                    if (!temporals_map->array[instr.info.ref_of.src.val].is_addressable) {

                        if (fn.error_info_list.array[instr.error_info_index].info.ref_of.is_from_match) {
                            const SV msg = SV_from_str_len("match expression must be addressable", 36);
                            type_error(&source_info->source, &source_info->source_filename, &fn.error_info_list.array[instr.error_info_index].info.ref_of.value.sv, &msg);
                            const SV hint = SV_from_str_len("try storing the value in a variable and then using the variable in the match expression", 88);
                            hint_standalone(&hint);
                            exit(1);
                        } else {
                            const SV msg = SV_from_str_len("could not be taken reference of", 31);
                            type_error(&source_info->source, &source_info->source_filename, &fn.error_info_list.array[instr.error_info_index].info.ref_of.value.sv, &msg);
                            const SV hint = SV_from_str_len("try storing the value in a variable and then using the variable here", 69);
                            hint_standalone(&hint);
                            exit(1);
                        }
                    }

                    printf("   %%%lu = REF_OF %%%lu\n", instr.info.ref_of.dest.val, instr.info.ref_of.src.val);

                    break;
                }
                case IR_INST_TY__VAR_DECL: {
                    temporals_map->array[instr.info.var_decl.dest.val].is_addressable = true;
                    const TC_typeid t = temporals_map->array[instr.info.var_decl.dest.val].type;
                    const TC_typeid original_t = resolve_tc_type(instr.info.var_decl.type, nct, target_info,
                                                                 source_info);
                    if (!is_type_eq(&t, &original_t)) {
                        INTERNAL_ERROR("should not happen")
                    }
                    const SV t_sv = format_TC_typeid(&t, identifier_map, nct);
                    char* value_vs_stack = "value";
                    for(size_t n = 0; n < promote_to_stack_temps.len; n++) {
                        if(promote_to_stack_temps.array[n].val == instr.info.var_decl.dest.val) {
                            value_vs_stack = "stack";
                            break;
                        }
                    }
                    
                    printf("   VAR_DECL %%%lu {on %s} <"SV_format">\n", instr.info.var_decl.dest.val, value_vs_stack, SV_v_args(t_sv));
                    break;
                }
                case IR_INST_TY__STORE_PTR: {
                    // todo: ADD autos perf -> ???
                    // printf("   STORE_PTR %%%lu VALUE %%%lu (not finished)\n", instr.info.store_ptr.ptr.val, instr.info.store_ptr.value.val);
                    // 1. The forward pass already processed INDEX_PTR, so the type is known!
                    TC_typeid ptr_ty = temporals_map->array[instr.info.store_ptr.ptr.val].type;

                    // 2. What type does this pointer hold? (e.g. Ref<Char> -> Char)
                    if (ptr_ty.ty != TC_TY_Ref) {
                        INTERNAL_ERROR();
                    }
                    TC_typeid expected_ty = *ptr_ty.info.contained; // unwrap_ref_once(ptr_ty);


                    // 3. Coerce the RHS value (e.g. Int32 '72') to match (Char)
                    IR_temporal coerced_val = perf_autos(
                        &instr.info.store_ptr.value,
                        &temporals_map->array[instr.info.store_ptr.value.val].type,
                        &expected_ty,
                        &highest_temporal,
                        identifier_map,
                        nct,
                        temporals_map,
                        source_info,
                        &fn.error_info_list.array[instr.error_info_index].info.store_ptr.expr.sv,
                        &temporals_map_values
                    );

                    // 4. Emit the finalized STORE instruction!
                    printf("   STORE_PTR INTO %%%lu VALUE %%%lu\n",
                           instr.info.store_ptr.ptr.val,
                           coerced_val.val);
                    break;
                }
                case IR_INST_TY__EXPLICIT_CAST: {
                    // can_be_explicit_casted
                    const TC_typeid src = temporals_map->array[instr.info.explicit_cast.src.val].type;
                    const TC_typeid cast_t = resolve_tc_type(instr.info.explicit_cast.type, nct, target_info,
                                                             source_info);

                    const SV cast_t_sv = format_TC_typeid(&cast_t, identifier_map, nct);

                    if (!can_be_explicit_casted(&src, &cast_t)) {
                        const SV whole = fn.error_info_list.array[instr.error_info_index].info.explicit_cast.whole.sv;
                        const SV expr = fn.error_info_list.array[instr.error_info_index].info.explicit_cast.expr.sv;
                        const SV type = fn.error_info_list.array[instr.error_info_index].info.explicit_cast.type.sv;

                        const size_t MAX_SIZE = 1024;
                        char* r_msg = malloc(sizeof(char) * MAX_SIZE);
                        const SV src_s = format_TC_typeid(&src, identifier_map, nct);
                        int len = snprintf(r_msg, MAX_SIZE,
                                           "cannot cast "C_BOLD""SV_format""C_RESET" into "C_BOLD""SV_format""C_RESET"",
                                           SV_v_args(src_s), SV_v_args(cast_t_sv));
                        const SV msg = SV_from_str_len(r_msg, len);
                        type_error(&source_info->source, &source_info->source_filename, &expr, &msg);
                        exit(1);
                    }
                    const TC_typeid dest_t = temporals_map->array[instr.info.explicit_cast.dest.val].type;
                    const SV dest_t_sv = format_TC_typeid(&dest_t, identifier_map, nct);

                    uint64_t out_temporal = instr.info.explicit_cast.dest.val;
                    if (!is_type_eq(&dest_t, &cast_t)) {
                        out_temporal = highest_temporal++;
                        if (out_temporal + 1 > temporals_map->cap) {
                            TC_Entry_LIST_inflate(temporals_map, out_temporal + 5);
                        }
                        if (!can_be_implicit_casted(&cast_t, &dest_t)) {
                            const SV whole = fn.error_info_list.array[instr.error_info_index].info.explicit_cast.whole.
                                    sv;
                            const SV expr = fn.error_info_list.array[instr.error_info_index].info.explicit_cast.expr.sv;
                            const SV type = fn.error_info_list.array[instr.error_info_index].info.explicit_cast.type.sv;

                            // expected <T> but found <T>
                            const size_t MAX_SIZE = 1024;
                            char* r_msg = malloc(sizeof(char) * MAX_SIZE);
                            int len = snprintf(r_msg, MAX_SIZE,
                                               "expected "C_BOLD""SV_format""C_RESET" but found "C_BOLD""SV_format""
                                               C_RESET"", SV_v_args(dest_t_sv), SV_v_args(cast_t_sv));
                            const SV msg = SV_from_str_len(r_msg, len);
                            type_error(&source_info->source, &source_info->source_filename, &whole, &msg);
                            exit(1);
                        } else {
                            printf("   [D] implicit type conversion from "SV_format" to "SV_format" is possible \n",
                                   SV_v_args(cast_t_sv), SV_v_args(dest_t_sv));
                            // print_TC_typeid(&cast_t, identifier_map, nct);
                            // printf(" to ");
                            // print_TC_typeid(&dest_t, identifier_map, nct);
                            // printf(" is possible \n");
                        }
                    }
                    // printf("   %%%lu = CAST<", out_temporal);
                    // print_TC_typeid(&cast_t, identifier_map, nct);
                    // printf(">(%%%lu) [EXPLICIT]\n", instr.info.explicit_cast.src.val);
                    printf("   %%%lu = CAST<"SV_format">(%%%lu) [EXPLICIT]\n", out_temporal, SV_v_args(cast_t_sv),
                           instr.info.explicit_cast.src.val);

                    if (out_temporal != instr.info.explicit_cast.dest.val) {
                        // printf("   %%%lu = CAST<", instr.info.explicit_cast.dest.val);
                        // print_TC_typeid(&dest_t, identifier_map, nct);
                        // printf(">(%%%lu) [IMPLICIT]\n", out_temporal);
                        printf("   %%%lu = CAST<"SV_format">(%%%lu) [IMPLICIT]\n", instr.info.explicit_cast.dest.val,
                               SV_v_args(dest_t_sv), out_temporal);

                        temporals_map->array[out_temporal] = (TC_Entry){
                            .known_temporal = true,
                            .type = dest_t,
                            .is_value_known = false,
                            .is_addressable = false
                        };
                    }
                    break;
                }
                case IR_INST_TY__CONST_INT: {
                    const TC_typeid dest_t = temporals_map->array[instr.info.const_int.dest.val].type;
                    const SV t_sv = format_TC_typeid(&dest_t,
                                                     identifier_map, nct);

                    if (dest_t.ty == TC_TY_Primitive && (
                        dest_t.info.primitive == TC_Primitive__Uint8 ||
                        dest_t.info.primitive == TC_Primitive__Uint16 ||
                        dest_t.info.primitive == TC_Primitive__Uint32 ||
                        dest_t.info.primitive == TC_Primitive__Uint64 ||
                        dest_t.info.primitive == TC_Primitive__Int8 ||
                        dest_t.info.primitive == TC_Primitive__Int16 ||
                        dest_t.info.primitive == TC_Primitive__Int32 ||
                        dest_t.info.primitive == TC_Primitive__Int64 ||
                        dest_t.info.primitive == TC_Primitive__Bool ||
                        dest_t.info.primitive == TC_Primitive__Char
                        )){
                    } else {
                        char r_msg[256] = {0};
                        const int len = snprintf(r_msg, sizeof(r_msg), "expected type "C_BOLD""SV_format""C_RESET", but found "C_BOLD"Integer"C_RESET" literal", SV_v_args(t_sv));
                        const SV msg = SV_from_str_len(r_msg, len);
                        type_error(&source_info->source, &source_info->source_filename, &fn.error_info_list.array[instr.error_info_index].info.literal.sv, &msg);
                        exit(1);
                    }

                    printf("   %%%lu = CONST_INT <"SV_format"> (%li)\n", instr.info.const_int.dest.val, SV_v_args(t_sv),
                           instr.info.const_int.val);
                    if (instr.info.const_int.dest.val + 1 > temporals_map_values.cap) {
                        TC_Value_LIST_inflate(&temporals_map_values, instr.info.const_int.dest.val + 1);
                    }
                    temporals_map_values.array[instr.info.const_int.dest.val] = (TC_Value){
                        .kind = TC_VK_Int,
                        .Int = instr.info.const_int.val
                    };
                    temporals_map->array[instr.info.const_int.dest.val].is_value_known = true;
                    break;
                }
                case IR_INST_TY__CONST_FLOAT: {
                    const TC_typeid dest_t = temporals_map->array[instr.info.const_float.dest.val].type;
                    const SV t_sv = format_TC_typeid(&dest_t,
                                                     identifier_map, nct);


                    if (dest_t.ty == TC_TY_Primitive && (dest_t.info.primitive == TC_Primitive__Float32 || dest_t.info.primitive == TC_Primitive__Float64)){
                    } else {
                        char r_msg[256] = {0};
                        const int len = snprintf(r_msg, sizeof(r_msg), "expected type "C_BOLD""SV_format""C_RESET", but found "C_BOLD"Float"C_RESET" literal", SV_v_args(t_sv));
                        const SV msg = SV_from_str_len(r_msg, len);
                        type_error(&source_info->source, &source_info->source_filename, &fn.error_info_list.array[instr.error_info_index].info.literal.sv, &msg);
                        exit(1);
                    }

                    printf("   %%%lu = CONST_FLOAT <"SV_format"> (%lf)\n", instr.info.const_float.dest.val,
                           SV_v_args(t_sv), instr.info.const_float.val);
                    if (instr.info.const_float.dest.val + 1 > temporals_map_values.cap) {
                        TC_Value_LIST_inflate(&temporals_map_values, instr.info.const_float.dest.val + 1);
                    }
                    temporals_map_values.array[instr.info.const_float.dest.val] = (TC_Value){
                        .kind = TC_VK_Float,
                        .Float = instr.info.const_float.val
                    };
                    temporals_map->array[instr.info.const_float.dest.val].is_value_known = true;
                    break;
                }
                case IR_INST_TY__CONST_STRING: {
                    const TC_typeid dest_t = temporals_map->array[instr.info.const_string.dest.val].type;
                    const SV t_sv = format_TC_typeid(&dest_t,
                                                     identifier_map, nct);

                    bool valid_t = false;
                    if (dest_t.ty == TC_TY_Slice || dest_t.ty == TC_TY_List) {
                        if (dest_t.info.contained->ty == TC_TY_Primitive && dest_t.info.contained->info.primitive == TC_Primitive__Char) {
                            valid_t = true;
                        }
                    } else if (dest_t.ty == TC_TY_Array) {
                        if (dest_t.info.t_array.contained->ty == TC_TY_Primitive && dest_t.info.t_array.contained->info.primitive == TC_Primitive__Char) {
                            valid_t = true;
                        }
                        const SV nk = SV_from_identifiermap_lookup(identifier_map, instr.info.const_string.string);
                        if (dest_t.info.t_array.array_size != nk.len) {
                            char r_msg[256] = {0};
                            const int len = snprintf(r_msg, sizeof(r_msg), "expected string of size %u, but found string of size %lu", dest_t.info.t_array.array_size, nk.len);
                            const SV msg = SV_from_str_len(r_msg, len);
                            type_error(&source_info->source, &source_info->source_filename, &fn.error_info_list.array[instr.error_info_index].info.literal.sv, &msg);
                            exit(1);
                        }
                    }

                    if (!valid_t) {
                        char r_msg[256] = {0};
                        const int len = snprintf(r_msg, sizeof(r_msg), "expected type "C_BOLD""SV_format""C_RESET", but found "C_BOLD"String"C_RESET" literal", SV_v_args(t_sv));
                        const SV msg = SV_from_str_len(r_msg, len);
                        type_error(&source_info->source, &source_info->source_filename, &fn.error_info_list.array[instr.error_info_index].info.literal.sv, &msg);
                        exit(1);
                    }

                    const SV val = SV_from_identifiermap_lookup(identifier_map, instr.info.const_string.string);
                    printf("   %%%lu = CONST_STRING <"SV_format"> (\"", instr.info.const_string.dest.val,
                           SV_v_args(t_sv));
                    for (size_t i = 0; i < val.len; i++) {
                        unsigned char c = val.start[i];
                        if (c == '\n') printf("\\n");
                        else if (c == '\t') printf("\\t");
                        else if (c == '\r') printf("\\r");
                        else if (c == '\\') printf("\\\\");
                        else if (isprint(c)) putchar(c);
                        else printf("\\x%02X", c);
                    }
                    printf("\")\n");
                    break;
                }
                case IR_INST_TY__OP_ADD:
                case IR_INST_TY__OP_SUB:
                case IR_INST_TY__OP_DIV:
                case IR_INST_TY__OP_MUL:
                case IR_INST_TY__OP_POW: {
                    bool is_error_out;
                    bool is_out_error_incompatible_number_relation;
                    const DoubleIR_temporal_and_TC_typeid res = perf_numeric_promotion(
                        &instr.info.math_cmp_boolean_op.src1,
                        &temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].type,
                        &instr.info.math_cmp_boolean_op.src2,
                        &temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].type,
                        &highest_temporal,
                        &is_error_out,
                        &is_out_error_incompatible_number_relation,
                        temporals_map,
                        &temporals_map_values
                    );

                    // numeric promotion failed, but which of the two? or both
                    if (is_error_out && !is_out_error_incompatible_number_relation) {
                        const bool error_a = res.a.val == 1;
                        const bool error_b = res.b.val == 1;

                        const SV val1_sv = fn.error_info_list.array[instr.error_info_index].info.math_operation.v1.sv;
                        const SV val2_sv = fn.error_info_list.array[instr.error_info_index].info.math_operation.v2.sv;

                        // const SV msg = SV_from_str_len("type ""`", 40);
                        if (error_a) {
                            const size_t MAX_SIZE = 1024;
                            char* r_msg = malloc(sizeof(char) * MAX_SIZE);
                            // const SV a_type_sv = temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].type.full_typeuse_span.sv;
                            const SV a_type_sv = format_TC_typeid(
                                &temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].type, identifier_map,
                                nct);
                            int len = snprintf(r_msg, MAX_SIZE,
                                               "type "C_BOLD""SV_format""C_RESET
                                               " could not be coerced to a numerical type", SV_v_args(a_type_sv));
                            const SV msg = SV_from_str_len(r_msg, len);
                            type_error(&source_info->source, &source_info->source_filename, &val1_sv, &msg);
                            free(r_msg);
                        }
                        if (error_b) {
                            const size_t MAX_SIZE = 1024;
                            char* r_msg = malloc(sizeof(char) * MAX_SIZE);
                            // const SV b_type_sv = temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].type.full_typeuse_span.sv;
                            const SV b_type_sv = format_TC_typeid(
                                &temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].type, identifier_map,
                                nct);
                            int len = snprintf(r_msg, MAX_SIZE,
                                               "type "C_BOLD""SV_format""C_RESET
                                               " could not be coerced to a numerical type", SV_v_args(b_type_sv));
                            const SV msg = SV_from_str_len(r_msg, len);
                            type_error(&source_info->source, &source_info->source_filename, &val2_sv, &msg);
                            free(r_msg);
                        }

                        exit(1);
                    } else if (is_error_out && is_out_error_incompatible_number_relation) {
                        // TODO: add known_value check to make var<Uint> + const<5> possible
                        const SV whole = fn.error_info_list.array[instr.error_info_index].info.math_operation.whole.sv;
                        const SV op = fn.error_info_list.array[instr.error_info_index].info.math_operation.op.sv;

                        const SV a_type = format_TC_typeid(
                            &temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].type, identifier_map, nct);
                        const SV b_type = format_TC_typeid(
                            &temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].type, identifier_map, nct);

                        const size_t MAX_SIZE = 1024;
                        char* r_msg = malloc(sizeof(char) * MAX_SIZE);
                        int len = snprintf(r_msg, MAX_SIZE,
                                           "cannot apply operator '"SV_format"' to mismatched types "C_BOLD""SV_format""
                                           C_RESET" and "C_BOLD""SV_format""C_RESET"",
                                           SV_v_args(op),
                                           SV_v_args(a_type),
                                           SV_v_args(b_type)
                        );
                        const SV msg = SV_from_str_len(r_msg, len);
                        type_error(&source_info->source, &source_info->source_filename, &whole, &msg);

                        const SV hint = SV_from_str_len("use an explicit cast on one of the operands", 43);
                        hint_standalone(&hint);
                        exit(1);
                    }

                    const SV res_t_sv = format_TC_typeid(&res.t, identifier_map, nct);
                    const TC_typeid dest_t = temporals_map->array[instr.info.math_cmp_boolean_op.dest.val].type;
                    const SV dest_t_sv = format_TC_typeid(&dest_t, identifier_map, nct);

                    uint64_t out_temporal = instr.info.math_cmp_boolean_op.dest.val;
                    if (
                        temporals_map->array[instr.info.math_cmp_boolean_op.dest.val].known_temporal &&
                        (dest_t.ty != TC_TY_Primitive ||
                         dest_t.info.primitive != res.t.info
                         .primitive)
                    ) {
                        out_temporal = highest_temporal++;


                        if (!can_be_implicit_casted(
                            &res.t, &dest_t)) {
                            const SV whole = fn.error_info_list.array[instr.error_info_index].info.math_operation.whole.
                                    sv;

                            // expected <T> but found <T>
                            const size_t MAX_SIZE = 1024;
                            char* r_msg = malloc(sizeof(char) * MAX_SIZE);

                            int len = snprintf(r_msg, MAX_SIZE,
                                               "expected "C_BOLD""SV_format""C_RESET" but found "C_BOLD""SV_format""
                                               C_RESET"", SV_v_args(dest_t_sv),SV_v_args(res_t_sv));
                            const SV msg = SV_from_str_len(r_msg, len);
                            type_error(&source_info->source, &source_info->source_filename, &whole, &msg);
                            exit(1);
                        } else {
                            printf("   [D] implicit type conversion from "SV_format" to "SV_format" is possible \n",
                                   SV_v_args(res_t_sv), SV_v_args(dest_t_sv));
                            // print_TC_typeid(&res.t, identifier_map, nct);
                            // printf(" to ");
                            // print_TC_typeid(&temporals_map->array[instr.info.math_cmp_boolean_op.dest.val].type, identifier_map, nct);
                            // printf(" is possible \n");
                        }
                    }
                    printf("   %%%lu = ", out_temporal);
                    switch (instr.ty) {
                        case IR_INST_TY__OP_ADD: {
                            printf("OP_ADD");
                            break;
                        }
                        case IR_INST_TY__OP_SUB: {
                            printf("OP_SUB");
                            break;
                        }
                        case IR_INST_TY__OP_DIV: {
                            printf("OP_DIV");
                            break;
                        }
                        case IR_INST_TY__OP_MUL: {
                            printf("OP_MUL");
                            break;
                        }
                        case IR_INST_TY__OP_POW: {
                            printf("OP_POW");
                            break;
                        }
                        default: INTERNAL_ERROR()
                    }
                    // printf(" <");
                    // print_TC_typeid(&res.t, identifier_map, nct);
                    // printf("> %%%lu, %%%lu\n", res.a.val, res.b.val);
                    printf(" <"SV_format"> %%%lu, %%%lu\n", SV_v_args(res_t_sv), res.a.val, res.b.val);

                    if (out_temporal != instr.info.math_cmp_boolean_op.dest.val) {
                        // printf("   %%%lu = CAST<", instr.info.math_cmp_boolean_op.dest.val);
                        // print_TC_typeid(&temporals_map->array[instr.info.math_cmp_boolean_op.dest.val].type, identifier_map, nct);
                        // printf(">(%%%lu) [IMPLICIT]\n", out_temporal);
                        printf("   %%%lu = CAST<"SV_format">(%%%lu) [IMPLICIT]\n",
                               instr.info.math_cmp_boolean_op.dest.val, SV_v_args(dest_t_sv), out_temporal);
                    }

                    if (!temporals_map->array[instr.info.math_cmp_boolean_op.dest.val].known_temporal) {
                        temporals_map->array[instr.info.math_cmp_boolean_op.dest.val] = (TC_Entry){
                            .known_temporal = true,
                            .type = res.t,
                            .is_addressable = false,
                            .is_value_known = false,
                        };
                    } else {
                        if (dest_t.ty == TC_TY_Ref && is_type_eq(&res.t, dest_t.info.contained)) {
                            printf("   STORE_PTR INTO %%%lu VALUE %%%lu\n", instr.info.math_cmp_boolean_op.dest.val, out_temporal);
                        }
                    }
                    break;
                }
                case IR_INST_TY__OP_OR:
                case IR_INST_TY__OP_AND: {
                    const TC_typeid src1_t = temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].type;
                    const TC_typeid src2_t = temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].type;
                    const TC_typeid dest_t = temporals_map->array[instr.info.math_cmp_boolean_op.dest.val].type;

                    const IR_temporal src1_it = perf_bool(&instr.info.math_cmp_boolean_op.src1, &src1_t,
                                                          &highest_temporal, temporals_map);
                    const IR_temporal src2_it = perf_bool(&instr.info.math_cmp_boolean_op.src2, &src2_t,
                                                          &highest_temporal, temporals_map);

                    static TC_typeid bool_t = {
                        .ty = TC_TY_Primitive,
                        .info.primitive = TC_Primitive__Bool,
                        .full_typeuse_span = {.sv = {"Bool", 4}}
                    };

                    uint64_t out_temporal = instr.info.math_cmp_boolean_op.dest.val;
                    if (dest_t.ty != TC_TY_Primitive || dest_t.info.primitive != TC_Primitive__Bool) {
                        out_temporal = highest_temporal++;

                        if (out_temporal + 1 > temporals_map->cap) {
                            TC_Entry_LIST_inflate(temporals_map, out_temporal + 5);
                        }
                        temporals_map->array[out_temporal] = (TC_Entry){
                            .type = dest_t,
                            .known_temporal = true,
                            .is_value_known = false,
                            .is_addressable = false,
                        };

                        if (!can_be_implicit_casted(&bool_t, &dest_t)) {
                            INTERNAL_ERROR("TYPE ERROR HERE");
                        } else {
                            printf("   [D] implicit type conversion from ");
                            print_TC_typeid(&bool_t, identifier_map, nct);
                            printf(" to ");
                            print_TC_typeid(&dest_t, identifier_map, nct);
                            printf(" is possible \n");
                        }
                    }

                    printf("   %%%lu = ", out_temporal);
                    switch (instr.ty) {
                        case IR_INST_TY__OP_OR: {
                            printf("OP_OR");
                            break;
                        }
                        case IR_INST_TY__OP_AND: {
                            printf("OP_AND");
                            break;
                        }
                        default: INTERNAL_ERROR()
                    }

                    printf(" <");
                    print_TC_typeid(&bool_t, identifier_map, nct);
                    printf("> %%%lu, %%%lu\n", src1_it.val, src2_it.val);

                    if (out_temporal != instr.info.math_cmp_boolean_op.dest.val) {
                        printf("   %%%lu = CAST<", instr.info.math_cmp_boolean_op.dest.val);
                        print_TC_typeid(&dest_t, identifier_map, nct);
                        printf(">(%%%lu) [IMPLICIT]\n", out_temporal);
                    }

                    break;
                }
                case IR_INST_TY__OP_CMP_EQ:
                case IR_INST_TY__OP_CMP_NOT_EQ:
                case IR_INST_TY__OP_CMP_LESS:
                case IR_INST_TY__OP_CMP_LESS_EQ:
                case IR_INST_TY__OP_CMP_GREATER:
                case IR_INST_TY__OP_CMP_GREATER_EQ: {
                    // convert to numerical
                    bool is_error_out;
                    bool is_out_error_incompatible_number_relation;

                    const DoubleIR_temporal_and_TC_typeid res = perf_numeric_promotion(
                        &instr.info.math_cmp_boolean_op.src1,
                        &temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].type,
                        &instr.info.math_cmp_boolean_op.src2,
                        &temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].type,
                        &highest_temporal,
                        &is_error_out,
                        &is_out_error_incompatible_number_relation,
                        temporals_map,
                        &temporals_map_values
                    );

                    // RESULT OF OPERATION IS BOOL TYPE
                    const TC_typeid res_t = (TC_typeid){
                        .ty = TC_TY_Primitive,
                        .info.primitive = TC_Primitive__Bool,
                        .full_typeuse_span = (TokenSpan){.sv = SV_from_str_len("Bool", 4)}
                    };


                    // numeric promotion failed, but which of the two? or both
                    if (is_error_out && !is_out_error_incompatible_number_relation) {
                        const bool error_a = res.a.val == 1;
                        const bool error_b = res.b.val == 1;

                        const SV val1_sv = fn.error_info_list.array[instr.error_info_index].info.math_operation.v1.sv;
                        const SV val2_sv = fn.error_info_list.array[instr.error_info_index].info.math_operation.v2.sv;

                        // const SV msg = SV_from_str_len("type "" could not be coerced to a numerical type", 40);
                        if (error_a) {
                            const size_t MAX_SIZE = 1024;
                            char* r_msg = malloc(sizeof(char) * MAX_SIZE);
                            // const SV a_type_sv = temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].type.full_typeuse_span.sv;
                            const SV a_type_sv = format_TC_typeid(
                                &temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].type, identifier_map,
                                nct);
                            int len = snprintf(r_msg, MAX_SIZE,
                                               "type "C_BOLD""SV_format""C_RESET
                                               " could not be coerced to a numerical type", SV_v_args(a_type_sv));
                            const SV msg = SV_from_str_len(r_msg, len);
                            type_error(&source_info->source, &source_info->source_filename, &val1_sv, &msg);
                            free(r_msg);
                        }
                        if (error_b) {
                            const size_t MAX_SIZE = 1024;
                            char* r_msg = malloc(sizeof(char) * MAX_SIZE);
                            // const SV b_type_sv = temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].type.full_typeuse_span.sv;
                            const SV b_type_sv = format_TC_typeid(
                                &temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].type, identifier_map,
                                nct);
                            int len = snprintf(r_msg, MAX_SIZE,
                                               "type "C_BOLD""SV_format""C_RESET
                                               " could not be coerced to a numerical type", SV_v_args(b_type_sv));
                            const SV msg = SV_from_str_len(r_msg, len);
                            type_error(&source_info->source, &source_info->source_filename, &val2_sv, &msg);
                            free(r_msg);
                        }

                        exit(1);
                    } else if (is_error_out && is_out_error_incompatible_number_relation) {
                        // TODO: add known_value check to make var<Uint> + const<5> possible
                        const SV whole = fn.error_info_list.array[instr.error_info_index].info.math_operation.whole.sv;
                        const SV op = fn.error_info_list.array[instr.error_info_index].info.math_operation.op.sv;

                        const SV a_type = format_TC_typeid(
                            &temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].type, identifier_map, nct);
                        const SV b_type = format_TC_typeid(
                            &temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].type, identifier_map, nct);

                        const size_t MAX_SIZE = 1024;
                        char* r_msg = malloc(sizeof(char) * MAX_SIZE);
                        int len = snprintf(r_msg, MAX_SIZE,
                                           "cannot apply operator '"SV_format"' to mismatched types "C_BOLD""SV_format""
                                           C_RESET" and "C_BOLD""SV_format""C_RESET"",
                                           SV_v_args(op),
                                           SV_v_args(a_type),
                                           SV_v_args(b_type)
                        );
                        const SV msg = SV_from_str_len(r_msg, len);
                        type_error(&source_info->source, &source_info->source_filename, &whole, &msg);

                        const SV hint = SV_from_str_len("use an explicit cast on one of the operands", 43);
                        hint_standalone(&hint);
                        exit(1);
                    }

                    const SV res_t_sv = SV_from_str_len("Bool", 4); //format_TC_typeid(&res.t, identifier_map, nct);
                    const SV dest_t_sv = format_TC_typeid(
                        &temporals_map->array[instr.info.math_cmp_boolean_op.dest.val].type, identifier_map, nct);

                    uint64_t out_temporal = instr.info.math_cmp_boolean_op.dest.val;
                    if (
                        temporals_map->array[instr.info.math_cmp_boolean_op.dest.val].type.ty != TC_TY_Primitive ||
                        temporals_map->array[instr.info.math_cmp_boolean_op.dest.val].type.info.primitive !=
                        TC_Primitive__Bool
                    ) {
                        out_temporal = highest_temporal++;


                        if (!can_be_implicit_casted(
                            &res_t, &temporals_map->array[instr.info.math_cmp_boolean_op.dest.val].type)) {
                            const SV whole = fn.error_info_list.array[instr.error_info_index].info.math_operation.whole.
                                    sv;

                            // expected <T> but found <T>
                            const size_t MAX_SIZE = 1024;
                            char* r_msg = malloc(sizeof(char) * MAX_SIZE);

                            int len = snprintf(r_msg, MAX_SIZE,
                                               "expected "C_BOLD""SV_format""C_RESET" but found "C_BOLD""SV_format""
                                               C_RESET"", SV_v_args(dest_t_sv),SV_v_args(res_t_sv));
                            const SV msg = SV_from_str_len(r_msg, len);
                            type_error(&source_info->source, &source_info->source_filename, &whole, &msg);
                            exit(1);
                        } else {
                            printf("   [D] implicit type conversion from "SV_format" to "SV_format" is possible \n",
                                   SV_v_args(res_t_sv), SV_v_args(dest_t_sv));

                            if (temporals_map->cap < out_temporal + 1) {
                                TC_Entry_LIST_inflate(temporals_map, out_temporal + 5);
                            }
                            temporals_map->array[out_temporal] = (TC_Entry){
                                .known_temporal = true,
                                .type = temporals_map->array[instr.info.math_cmp_boolean_op.dest.val].type,
                                .is_value_known = false,
                                .is_addressable = false,
                            };
                            // print_TC_typeid(&res_t, identifier_map, nct);
                            // printf(" to ");
                            // print_TC_typeid(&temporals_map->array[instr.info.math_cmp_boolean_op.dest.val].type, identifier_map, nct);
                            // printf(" is possible \n");
                        }
                    }
                    printf("   %%%lu = ", out_temporal);
                    switch (instr.ty) {
                        case IR_INST_TY__OP_CMP_EQ: {
                            printf("CMP_EQ");
                            break;
                        }
                        case IR_INST_TY__OP_CMP_NOT_EQ: {
                            printf("CMP_NOT_EQ");
                            break;
                        }
                        case IR_INST_TY__OP_CMP_LESS: {
                            printf("CMP_LESS");
                            break;
                        }
                        case IR_INST_TY__OP_CMP_LESS_EQ: {
                            printf("CMP_LESS_EQ");
                            break;
                        }
                        case IR_INST_TY__OP_CMP_GREATER: {
                            printf("CMP_GREATER");
                            break;
                        }
                        case IR_INST_TY__OP_CMP_GREATER_EQ: {
                            printf("CMP_GREATER_EQ");
                            break;
                        }
                        default: INTERNAL_ERROR()
                    }
                    printf(" <"SV_format"> %%%lu, %%%lu\n", SV_v_args(res_t_sv), res.a.val, res.b.val);

                    if (out_temporal != instr.info.math_cmp_boolean_op.dest.val) {
                        printf("   %%%lu = CAST<"SV_format">(%%%lu) [IMPLICIT]\n",
                               instr.info.math_cmp_boolean_op.dest.val, SV_v_args(dest_t_sv), out_temporal);
                    }
                    break;
                }
                case IR_INST_TY__INDEX_PTR: {
                    // 1. What are we indexing into? (e.g., %11 which is List<Char>)
                    const TC_typeid source_list_ty = temporals_map->array[instr.info.index_ptr.array_ptr.val].type;


                    // const IR_temporal_and_TC_type list = perf_auto_deref(&source_list_ty, &instr.info.index_ptr.array_ptr, &highest_temporal, temporals_map);
                    // if (source_list_ty.ty != TC_TY_Ref) {
                    //     // type error
                    //     const SV source_list_ty_sv = format_TC_typeid(&source_list_ty, identifier_map, nct);
                    //     char r_msg[1024] = {0};
                    //     int len = snprintf(r_msg, sizeof(r_msg), "type "C_BOLD""SV_format""C_RESET" cannot be indexed",
                    //                        SV_v_args(source_list_ty_sv));
                    //     const SV msg = SV_from_str_len(r_msg, len);
                    //     type_error(&source_info->source, &source_info->source_filename,
                    //                &fn.error_info_list.array[instr.error_info_index].info.index_array.index_expr.sv,
                    //                &msg);
                    //     exit(1);
                    // }
                    const IR_temporal_and_TC_type list = perf_auto_deref(&source_list_ty, &instr.info.index_ptr.array_ptr, &highest_temporal, temporals_map);
                    const SV list_ty_sv = format_TC_typeid(&list.type, identifier_map, nct);

                    // const IR_temporal_and_TC_type list = (IR_temporal_and_TC_type) {
                    //     .temporal = instr.info.index_ptr.array_ptr,
                    //     .type = *source_list_ty.info.contained
                    // };
                    // const SV list_ty_sv = format_TC_typeid(&list.type, identifier_map, nct);

                    // 2. Extract the inner element type ('Char')
                    // TODO: assert array/list size check
                    TC_typeid* element_ty;
                    switch (list.type.ty) {
                        case TC_TY_Void: INTERNAL_ERROR()
                        case TC_TY_Primitive:
                        case TC_TY_Struct:
                        case TC_TY_Box:
                        case TC_TY_Ref:
                        case TC_TY_External_Type:
                        case TC_TY_Enum: {
                            // type error
                            char r_msg[1024] = {0};
                            int len = snprintf(r_msg, sizeof(r_msg), "type "C_BOLD""SV_format""C_RESET" cannot be indexed",
                                               SV_v_args(list_ty_sv));
                            const SV msg = SV_from_str_len(r_msg, len);
                            type_error(&source_info->source, &source_info->source_filename,
                                       &fn.error_info_list.array[instr.error_info_index].info.index_array.index_expr.sv,
                                       &msg);
                            exit(1);
                        }
                        case TC_TY_List:
                        case TC_TY_Slice: {
                            element_ty = list.type.info.contained;
                            break;
                        }
                        case TC_TY_Array: {
                            element_ty = list.type.info.t_array.contained;
                            break;
                        }
                    }
                    // = source_list_ty.info.contained; // get_list_inner_type(source_list_ty);
                    const SV ele_sv = format_TC_typeid(element_ty, identifier_map, nct);
                    // 3. Wrap it in a Reference! ('Ref<Char>')
                    // TC_typeid ref_ty = wrap_in_ref(element_ty);
                    const size_t MAX_SIZE = 256;
                    char* rs = malloc(sizeof(char) * MAX_SIZE);
                    int len = snprintf(rs, MAX_SIZE, "Ref<"SV_format">", SV_v_args(ele_sv));
                    rs = realloc(rs, len);
                    const SV ref_span = SV_from_str_len(rs, len);

                    const TC_typeid ref_ty = (TC_typeid){
                        .ty = TC_TY_Ref,
                        .info.contained = (TC_typeid *) element_ty,
                        .full_typeuse_span = (TokenSpan){.sv = ref_span}
                    };

                    // 4. Assign this type to the destination temporal (%13)
                    temporals_map->array[instr.info.index_ptr.dest.val].type = ref_ty;

                    // 5. Emit the instruction
                    printf("   %%%lu = INDEX_PTR %%%lu [ %%%lu ]\n",
                           instr.info.index_ptr.dest.val,
                           //instr.info.index_ptr.array_ptr.val,
                           list.temporal.val,
                           instr.info.index_ptr.index_val.val);
                    break;
                }
                case IR_INST_TY__READ_PTR: {
                    printf("   %%%lu = READ_PTR %%%lu\n", instr.info.read_ptr.dest.val, instr.info.read_ptr.ptr.val);
                    break;
                }
                case IR_INST_TY__MAKE_STRUCT: {
                    const TC_typeid struct_type = temporals_map->array[instr.info.make_struct.dest.val].type;
                    if (struct_type.ty != TC_TY_Struct) {
                        INTERNAL_ERROR()
                    }
                    const TC_structdef sdef = nct->structs.array[struct_type.info.type_id];
                    IR_temporal_LIST args = IR_temporal_LIST_new_with_capacity(sdef.fields.len);
                    for (size_t n = 0; n < sdef.fields.len; n++) {
                        IR_temporal_LIST_push(&args, (IR_temporal){.val = 0});
                    }

                    for (size_t k = 0; k < instr.info.make_struct.args->len; k++) {
                        const IR_named_value arg = instr.info.make_struct.args->array[k];
                        bool found = false;
                        for (size_t n = 0; n < sdef.fields.len; n++) {
                            if (sdef.fields.array[n].name.identifier == arg.name.identifier) {
                                if (args.array[n].val != 0) {
                                    // INTERNAL_ERROR("duplicate field");
                                    const SV msg = SV_from_str("duplicate field");
                                    syntax_error(&source_info->source, &source_info->source_filename, &arg.name.span.sv,
                                                 &msg);
                                    exit(1);
                                }

                                if (!temporals_map->array[arg.value.val].known_temporal) {
                                    INTERNAL_ERROR("UNKNOWN");
                                }
                                const TC_typeid arg_t = temporals_map->array[arg.value.val].type;
                                const IR_temporal out = perf_autos(
                                    &arg.value,
                                    &arg_t,
                                    &sdef.fields.array[n].type,
                                    &highest_temporal,
                                    identifier_map,
                                    nct,
                                    temporals_map,
                                    source_info,
                                    &fn.error_info_list.array[instr.error_info_index].info.make_struct.values[k].sv,
                                    &temporals_map_values
                                );
                                args.array[n] = out;
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            INTERNAL_ERROR("type error")
                        }
                        // arg.name.identifier
                    }

                    for (size_t n = 0; n < sdef.fields.len; n++) {
                        if (args.array[n].val == 0) {
                            INTERNAL_ERROR("missing arg for struct construction")
                        }
                    }

                    printf("   %%%lu = MAKE_STRUCT (%u size, %u align) { ", instr.info.make_struct.dest.val,
                           sdef.layout.size, sdef.layout.align);
                    for (size_t n = 0; n < sdef.fields.len; n++) {
                        printf("%%%lu at %u", args.array[n].val, sdef.layout.field_offsets[n]);
                        if (n + 1 != sdef.fields.len) printf(", ");
                        else printf(" ");
                    }
                    printf("}\n");
                    break;
                }
                case IR_INST_TY__MAKE_ENUM: {
                    const TC_typeid enum_type = temporals_map->array[instr.info.make_enum.dest.val].type;
                    if (enum_type.ty != TC_TY_Enum) {
                        INTERNAL_ERROR()
                    }
                    const TC_enumdef edef = nct->enums.array[enum_type.info.type_id];
                    uint64_t variant_n;
                    bool variant_n_found = false;
                    for (uint64_t v = 0; v < edef.variants.len; v++) {
                        if (edef.variants.array[v].name.identifier == instr.info.make_enum.variant) {
                            variant_n = v;
                            variant_n_found = true;
                            break;
                        }
                    }
                    if (!variant_n_found)
                        INTERNAL_ERROR()

                    IR_temporal_LIST args = IR_temporal_LIST_new_with_capacity(
                        edef.variants.array[variant_n].values_len);
                    for (size_t n = 0; n < edef.variants.array[variant_n].values_len; n++) {
                        const TC_typeid arg_t = edef.variants.array[variant_n].values[n];
                        const IR_temporal arg_v = instr.info.make_enum.args->array[n];
                        const TokenSpan arg_span = fn.error_info_list.array[instr.error_info_index].info.make_enum.items
                                [n];

                        const TC_typeid arg_v_t = temporals_map->array[arg_v.val].type;
                        const IR_temporal out = perf_autos(
                            &arg_v,
                            &arg_v_t,
                            &arg_t,
                            &highest_temporal,
                            identifier_map,
                            nct,
                            temporals_map,
                            source_info,
                            &arg_span.sv,
                            &temporals_map_values
                        );
                        IR_temporal_LIST_push(&args, out);
                    }
                    printf("   %%%lu = MAKE_ENUM (size %u, align %u) { .variant_val=%lu", instr.info.make_enum.dest.val,
                           edef.size, edef.align, variant_n);
                    if (args.len > 0) printf(", .value=[");
                    else printf(" ");

                    for (size_t n = 0; n < args.len; n++) {
                        printf("%%%lu", args.array[n].val);
                        if (n + 1 != args.len) printf(", ");
                    }
                    if (args.len > 0) printf("] ");
                    printf("}\n");

                    break;
                }
                case IR_INST_TY__MAKE_ARRAY: {
                    // 1. case -> dest known
                    // 2, case -> dest unknown
                    if (!temporals_map->array[instr.info.make_array.dest.val].known_temporal) {
                        INTERNAL_ERROR("MAKE_ARRAY UNKNOWN DEST")
                    }
                    const TC_typeid dest_t = temporals_map->array[instr.info.make_array.dest.val].type;
                    TC_typeid base_type = dest_t;
                    while (base_type.ty != TC_TY_Array && base_type.ty != TC_TY_List) {
                        switch (base_type.ty) {
                            case TC_TY_Ref:
                            case TC_TY_Box: {
                                base_type = *base_type.info.contained;
                                break;
                            }
                            default:
                                INTERNAL_ERROR("INTERNAL TYPE ERROR")
                        }
                    }
                    TC_typeid inner_type;
                    switch (base_type.ty) {
                        case TC_TY_Array: {
                            inner_type = *base_type.info.t_array.contained;
                            break;
                        }
                        case TC_TY_List: {
                            inner_type = *base_type.info.contained;
                            break;
                        }
                        default: INTERNAL_ERROR("INTERNAL TYPE ERROR")
                    }

                    IR_temporal_LIST items = IR_temporal_LIST_new_with_capacity(instr.info.make_array.items->len);
                    for (size_t n = 0; n < instr.info.make_array.items->len; n++) {
                        const IR_temporal item = instr.info.make_array.items->array[n];
                        const TC_typeid item_t = temporals_map->array[item.val].type;
                        const IR_temporal out = perf_autos(
                            &item,
                            &item_t,
                            &inner_type,
                            &highest_temporal,
                            identifier_map,
                            nct,
                            temporals_map,
                            source_info,
                            &fn.error_info_list.array[instr.error_info_index].info.make_array.items[n].sv,
                            &temporals_map_values
                        );
                        IR_temporal_LIST_push(&items, out);
                    }

                    const SizeAlign sa = calc_size_of(&base_type, nct, target_info);
                    const SizeAlign it_sa = calc_size_of(&inner_type, nct, target_info);
                    printf("   %%%lu = MAKE_ARRAY (stack: size %lu, align %lu) ", instr.info.make_array.dest.val,
                           sa.size, sa.align);
                    if (base_type.ty == TC_TY_Array) {
                        printf("{STACK ARRAY}");
                    } else if (base_type.ty == TC_TY_List) {
                        printf("{HEAP ARRAY}");
                        // instr.info.make_array.items->len;

                        printf(" {%lu bytes on heap, %lu align}", it_sa.size * instr.info.make_array.items->len,
                               it_sa.align);
                    } else {
                        INTERNAL_ERROR("unknown list type");
                    }
                    printf(" [ ");
                    for (size_t n = 0; n < items.len; n++) {
                        printf("%%%lu", items.array[n].val);
                        if (n + 1 != items.len) printf(", ");
                        else printf(" ");
                    }
                    printf("]\n");

                    break;
                }
                case IR_INST_TY__BRANCH: {
                    const TC_typeid t = temporals_map->array[instr.info.branch.cond.val].type;

                    const IR_temporal out = perf_bool(
                        &instr.info.branch.cond,
                        &t,
                        &highest_temporal,
                        temporals_map
                    );
                    printf("   BRANCH <Bool> %%%lu, Block %lu, Block %lu\n", out.val, instr.info.branch.branch_true,
                           instr.info.branch.branch_false);
                    break;
                }
                case IR_INST_TY__JUMP: {
                    printf("   JUMP Block %lu\n", instr.info.jump.block);
                    break;
                }
                case IR_INST_TY__RETURN_VOID: {
                    printf("   RETURN\n");
                    break;
                }
                case IR_INST_TY__RETURN: {
                    if (fn.fn_ret_t.tt_ty == IR_TY_Void) {
                        const SV msg = SV_from_str_len("expected no value on return based on function signature", 55);
                        type_error(&source_info->source, &source_info->source_filename,
                                   &fn.error_info_list.array[instr.error_info_index].info.return_value.expr.sv, &msg);
                        exit(1);
                    }
                    const TC_typeid t = resolve_tc_type(&fn.fn_ret_t, nct, target_info, source_info);
                    const TC_typeid intake_t = temporals_map->array[instr.info.return_value.val].type;
                    const IR_temporal out = perf_autos(&instr.info.return_value, &intake_t, &t, &highest_temporal,
                                                       identifier_map, nct, temporals_map, source_info,
                                                       &fn.error_info_list.array[instr.error_info_index].info.
                                                       return_value.expr.sv, &temporals_map_values);
                    const SV out_t_sv = format_TC_typeid(&t, identifier_map, nct);
                    printf("   RETURN <"SV_format"> %%%lu\n", SV_v_args(out_t_sv), out.val);
                    break;
                }
                case IR_INST_TY__GET_STRUCT_FIELD_PTR: {
                    // instr.info.get_struct_field_ptr.struct_ptr
                    const TC_typeid struct_ptr_t = temporals_map->array[instr.info.get_struct_field_ptr.struct_ptr.val].
                            type;
                    const SV struct_ptr_t_sv = format_TC_typeid(&struct_ptr_t, identifier_map, nct);
                    if (struct_ptr_t.ty != TC_TY_Ref && !temporals_map->array[instr.info.get_struct_field_ptr.struct_ptr
                            .val].is_addressable) {
                        INTERNAL_ERROR("expected REF<T> or addressable value")
                    }
                    const TC_typeid struct_t = struct_ptr_t.ty == TC_TY_Ref
                                                   ? *struct_ptr_t.info.contained
                                                   : struct_ptr_t;
                    const SV struct_t_sv = format_TC_typeid(&struct_t, identifier_map, nct);
                    const TC_typeid dest_t = temporals_map->array[instr.info.get_struct_field_ptr.dest.val].type;

                    bool break_out = false;
                    TC_structdef sdef;
                    switch (struct_t.ty) {
                        case TC_TY_Struct: {
                            sdef = nct->structs.array[struct_t.info.type_id];
                            break;
                        }
                        case TC_TY_List: {
                            sdef = compiler_builtin__list_tc_structdef;
                            break;
                        }
                        case TC_TY_Slice: {
                            sdef = compiler_builtin__slice_tc_structdef;
                            break;
                        }
                        case TC_TY_Array: {
                            // coerce info.t_array.len->temporla
                            // UNIMPLEMENTED("ARRAY.len")

                            if (!SV_pv_cmp_eq(&instr.info.get_struct_field_ptr.field_name.span.sv, "len", 3)) {
                                const size_t MAX_SIZE = 512;
                                char* r_msg = malloc(sizeof(char) * MAX_SIZE);
                                int len = snprintf(r_msg, MAX_SIZE,
                                                   "struct "C_BOLD"Array"C_RESET" does not have field '"SV_format"",
                                                   SV_v_args(instr.info.get_struct_field_ptr.field_name.span.sv));
                                const SV msg = SV_from_str_len(r_msg, len);
                                syntax_error(&source_info->source, &source_info->source_filename,
                                             &instr.info.get_struct_field_ptr.field_name.span.sv, &msg);
                                exit(1);
                            }

                            // reuse Uint32 type from len of list
                            const TC_typeid field_t = compiler_builtin__list_tc_structdef.fields.array[0].type;
                            if (is_type_eq(&field_t, &dest_t)) {
                                const SV dest_t_sv = format_TC_typeid(&dest_t, identifier_map, nct);
                                const SV field_t_sv = format_TC_typeid(&field_t, identifier_map, nct);

                                const size_t MAX_SIZE = 512;
                                char* r_msg = malloc(sizeof(char) * MAX_SIZE);
                                int len = snprintf(r_msg, MAX_SIZE,
                                                   "expected value of type "C_BOLD""SV_format""C_RESET" but got "C_BOLD
                                                   ""SV_format""C_RESET"", SV_v_args(dest_t_sv), SV_v_args(field_t_sv));
                                const SV msg = SV_from_str_len(r_msg, len);

                                type_error(&source_info->source, &source_info->source_filename,
                                           &fn.error_info_list.array[instr.error_info_index].info.get_struct_field.whole
                                           .sv, &msg);
                                exit(1);
                            }


                            printf("   %%%lu = CONST_INT <Uint32> (%u)\n", instr.info.get_struct_field_ptr.dest.val,
                                   struct_t.info.t_array.array_size);

                            break_out = true;
                            break;
                        }
                        default: {
                            const size_t MAX_SIZE = 512;
                            char* r_msg = malloc(sizeof(char) * MAX_SIZE);
                            int len = snprintf(r_msg, MAX_SIZE,
                                               "expected structure, but provided value has type "C_BOLD""SV_format""
                                               C_RESET, SV_v_args(struct_t_sv));
                            const SV msg = SV_from_str_len(r_msg, len);
                            type_error(&source_info->source, &source_info->source_filename,
                                       &fn.error_info_list.array[instr.error_info_index].info.get_struct_field.origin.
                                       sv, &msg);
                            exit(1);
                            break;
                        }
                    }
                    if (break_out) {
                        break;
                    }


                    bool found = false;
                    for (size_t n = 0; n < sdef.fields.len; n++) {
                        if (sdef.fields.array[n].name.identifier == instr.info.get_struct_field_ptr.field_name.
                            identifier) {
                            // todo: load type; coerce to dest
                            const TC_typeid field_t = sdef.fields.array[n].type;
                            const SV field_t_sv = format_TC_typeid(&field_t, identifier_map, nct);
                            const SizeAlign field_sz = calc_size_of(&field_t, nct, target_info);


                            if (is_type_eq(&field_t, &dest_t)) {
                                const SV dest_t_sv = format_TC_typeid(&dest_t, identifier_map, nct);

                                const size_t MAX_SIZE = 512;
                                char* r_msg = malloc(sizeof(char) * MAX_SIZE);
                                int len = snprintf(r_msg, MAX_SIZE,
                                                   "expected value of type "C_BOLD""SV_format""C_RESET" but got "C_BOLD
                                                   ""SV_format""C_RESET"", SV_v_args(dest_t_sv), SV_v_args(field_t_sv));
                                const SV msg = SV_from_str_len(r_msg, len);

                                type_error(&source_info->source, &source_info->source_filename,
                                           &fn.error_info_list.array[instr.error_info_index].info.get_struct_field.whole
                                           .sv, &msg);
                                exit(1);
                            }

                            // %n = GET_STRUCT_PTR offset x bytes, read size x bytes, read type <>, ptr %x, ptr type <>
                            printf(
                                "   %%%lu = GET_STRUCT_FIELD_PTR offset %u bytes, read size %lu bytes, read align %lu bytes, read type "
                                SV_format", ptr %%%lu, ptr type "SV_format"\n",
                                instr.info.get_struct_field_ptr.dest.val,
                                sdef.layout.field_offsets[n],
                                field_sz.size,
                                field_sz.align,
                                SV_v_args(field_t_sv),
                                instr.info.get_struct_field_ptr.struct_ptr.val,
                                SV_v_args(struct_ptr_t_sv)
                            );

                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        const size_t MAX_SIZE = 512;
                        char* r_msg = malloc(sizeof(char) * MAX_SIZE);
                        int len = snprintf(r_msg, MAX_SIZE,
                                           "struct "C_BOLD""SV_format""C_RESET" does not have field '"SV_format"",
                                           SV_v_args(struct_t_sv),
                                           SV_v_args(instr.info.get_struct_field_ptr.field_name.span.sv));
                        const SV msg = SV_from_str_len(r_msg, len);
                        syntax_error(&source_info->source, &source_info->source_filename,
                                     &instr.info.get_struct_field_ptr.field_name.span.sv, &msg);
                        exit(1);
                    }


                    break;
                }
                case IR_INST_TY__FN_CALL_RETVOID:
                case IR_INST_TY__FN_CALL_RETVAL: {
                    const bool is_retval = (instr.ty == IR_INST_TY__FN_CALL_RETVAL);
                    const IR_temporal_LIST* raw_args = is_retval
                                                           ? instr.info.fn_call_retval.args
                                                           : instr.info.fn_call_retvoid.args;
                    const Identifier fn_name_id = is_retval
                                                      ? instr.info.fn_call_retval.name.identifier
                                                      : instr.info.fn_call_retvoid.name.identifier;


                    // 1. Find the target function
                    const IR_func* target_fn = NULL;
                    for (size_t n = 0; n < functions->len; n++) {
                        if (functions->array[n].fn_name.identifier == fn_name_id) {
                            target_fn = &functions->array[n];
                            break;
                        }
                    }
                    if (!target_fn) {
                        // look into generic functions
                        // generic cash name + <T, T, T,...>

                        const TC_typeid* expected_ret_t = is_retval
                                                              ? &temporals_map->array[instr.info.fn_call_retval.dest.
                                                                  val].type
                                                              : NULL;

                        for (size_t n = 0; n < nct->generic_functions.len; n++) {
                            if (nct->generic_functions.array[n].fn.fn_name.identifier != fn_name_id) continue;

                            // check materialization
                            // return function
                            const IR_generic_function gf = nct->generic_functions.array[n];

                            TC_typeid_LIST found_args_t = TC_typeid_LIST_new_with_capacity(raw_args->len);
                            for (size_t a = 0; a < raw_args->len; a++) {
                                const IR_temporal arg_temp = raw_args->array[a];
                                const TC_typeid arg_t = temporals_map->array[arg_temp.val].type;
                                TC_typeid_LIST_push(&found_args_t, arg_t);
                            }

                            const IR_func* rs = materialize_function(&gf, found_args_t, expected_ret_t, nct);
                            target_fn = rs;

                            break;
                        }
                    }
                    if (!target_fn) {
                        const TokenSpan fn_name_span = is_retval
                                                           ? instr.info.fn_call_retval.name.span
                                                           : instr.info.fn_call_retvoid.name.span;


                        const SV msg = SV_from_str_len("unknown function", 16);

                        syntax_error(&source_info->source, &source_info->source_filename, &fn_name_span.sv, &msg);
                        exit(1);
                    }

                    const SV raw_fn_name = target_fn->raw_name;

                    // 2. Coerce arguments using perf_autos
                    IR_temporal_LIST coerced_args = IR_temporal_LIST_new_with_capacity(raw_args->len);
                    TC_typeid_LIST coerced_args_types = TC_typeid_LIST_new_with_capacity(raw_args->len);

                    for (size_t k = 0; k < raw_args->len; k++) {
                        const IR_temporal arg_temp = raw_args->array[k];
                        const TC_typeid arg_t = temporals_map->array[arg_temp.val].type;
                        const TC_typeid expected_t = resolve_tc_type(&target_fn->args.array[k].type, nct,
                                                                     target_info, source_info);

                        const TokenSpan arg_span = fn.error_info_list.array[instr.error_info_index].info.function_call.
                                args[k];

                        const IR_temporal coerced = perf_autos(
                            &arg_temp,
                            &arg_t,
                            &expected_t,
                            &highest_temporal,
                            identifier_map,
                            nct,
                            temporals_map,
                            source_info,
                            &arg_span.sv,
                            &temporals_map_values
                        );
                        IR_temporal_LIST_push(&coerced_args, coerced);
                        TC_typeid_LIST_push(&coerced_args_types, expected_t);
                    }

                    bool emit_drop_for_ret = false;
                    // 3. Handle return value for RETVAL
                    if (is_retval) {
                        const TC_typeid expected_ret_t = temporals_map->array[instr.info.fn_call_retval.dest.val].type;
                        const TC_typeid fn_ret_t = resolve_tc_type(&target_fn->fn_ret_t, nct, target_info, source_info);


                        if (expected_ret_t.ty == TC_TY_Void) {
                            const size_t MAX_SIZE = 256;
                            char* r_msg = malloc(sizeof(char) * MAX_SIZE);
                            int len = snprintf(r_msg, MAX_SIZE, "'"SV_format"' function's return value discarded",
                                               SV_v_args(target_fn->fn_name.span.sv));
                            const SV msg = SV_from_str_len(r_msg, len);
                            warn_on_code(&source_info->source, &source_info->source_filename,
                                         &fn.error_info_list.array[instr.error_info_index].info.function_call.whole.sv,
                                         &msg);
                        }

                        // const IR_temporal ot = expected_ret_t.ty == TC_TY_Void ?
                        //     instr.info.fn_call_retval.dest :
                        //     perf_autos(
                        //         &instr.info.fn_call_retval.dest,
                        //         &fn_ret_t,
                        //         &expected_ret_t,
                        //         &highest_temporal,
                        //         identifier_map,
                        //         nct,
                        //         temporals_map,
                        //         source_info,
                        //         &fn.error_info_list.array[instr.error_info_index].info.function_call.whole.sv,
                        //         &temporals_map_values
                        //     )
                        // ;

                        if (expected_ret_t.ty == TC_TY_Void) {
                            emit_drop_for_ret = true;
                        }

                        temporals_map->array[instr.info.fn_call_retval.dest.val] = (TC_Entry){
                            .known_temporal = true,
                            .type = fn_ret_t,
                            .is_value_known = false,
                            .is_addressable = false
                        };

                        // const TC_typeid expected_ret_t_2 = temporals_map->array[instr.info.fn_call_retval.dest.val].type;
                        const SV expected_ret_t_sv = format_TC_typeid(&fn_ret_t, identifier_map, nct);
                        printf("   %%%lu <"SV_format"> = ", instr.info.fn_call_retval.dest.val,
                               SV_v_args(expected_ret_t_sv));

                    } else {
                        printf("   ");
                    }

                    // 4. Print / Emit MIR instruction
                    // ... print function name, destination, and coerced_args ...

                    printf("FUNC_CALL '"SV_format"' {raw='"SV_format"'} [", SV_v_args(target_fn->fn_name.span.sv),
                           SV_v_args(raw_fn_name));
                    for (size_t n = 0; n < coerced_args.len; n++) {
                        const SV arg_t_sv = format_TC_typeid(&coerced_args_types.array[n], identifier_map, nct);
                        printf("%%%lu <"SV_format">", coerced_args.array[n].val, SV_v_args(arg_t_sv));
                        if (n + 1 != coerced_args.len) {
                            printf(", ");
                        }
                    }
                    printf("]\n");

                    if (emit_drop_for_ret) {
                        const TC_typeid ret_t = temporals_map->array[instr.info.fn_call_retval.dest.val].type;
                        const SV ret_t_sv = format_TC_typeid(&ret_t, identifier_map, nct);
                        printf("   DROP <"SV_format"> %%%lu [STACK]\n", SV_v_args(ret_t_sv),
                               instr.info.fn_call_retval.dest.val);
                    }
                    break;
                }
                case IR_INST_TY__MATCH_META__TARGET: {
                    const TC_typeid target_t = temporals_map->array[instr.info.match_meta__target.target.val].type;
                    // const IR_temporal_and_TC_type res = perf_auto_deref(&target_t, &instr.info.match_meta__target.target, &highest_temporal, temporals_map);

                    TC_typeid res_t = target_t;
                    while (res_t.ty == TC_TY_Ref || res_t.ty == TC_TY_Box) res_t = *res_t.info.contained;

                    // const TC_typeid res_t = res.type;

                    if (res_t.ty != TC_TY_Enum && res_t.ty != TC_TY_Primitive) {
                        const SV target_t_sv = format_TC_typeid(&target_t, identifier_map, nct);
                        const size_t MAX_SIZE = 512;
                        char* r_msg = malloc(sizeof(char) * MAX_SIZE);
                        int len = snprintf(r_msg, MAX_SIZE,
                                           "expected either primitive or enum value but got value of type "C_BOLD""
                                           SV_format""C_RESET"", SV_v_args(target_t_sv));

                        const SV msg = SV_from_str_len(r_msg, len);
                        type_error(&source_info->source, &source_info->source_filename,
                                   &fn.error_info_list.array[instr.error_info_index].info.match_meta__target.expr.sv,
                                   &msg);
                        exit(1);
                    }
                    break;
                }
                case IR_INST_TY__MATCH_META__MATCH: {
                    TC_typeid target_t = temporals_map->array[instr.info.match_meta__match.target.val].type;
                    while (target_t.ty == TC_TY_Ref || target_t.ty == TC_TY_Box) target_t = *target_t.info.contained;

                    if (target_t.ty == TC_TY_Enum) {
                        const TC_enumdef edef = nct->enums.array[target_t.info.type_id];

                        bool* covered = calloc(edef.variants.len, sizeof(bool));
                        bool has_global_wildcard = false;

                        for (size_t n = 0; n < instr.info.match_meta__match.patterns.len; n++) {
                            const Pattern p = instr.info.match_meta__match.patterns.array[n];

                            // Top-level wildcard or binding covers everything
                            if (p.kind == PAT_WILDCARD || p.kind == PAT_BINDING) {
                                has_global_wildcard = true;
                                break;
                            } else if (p.kind == PAT_VARIANT) {
                                if (p.info.variant.enum_name.identifier != edef.name.identifier) {
                                    char r_msg[256] = {0};
                                    int len = snprintf(r_msg, sizeof(r_msg), "expected enum '"SV_format"', but got '"SV_format"'", SV_v_args(edef.name.span.sv), SV_v_args(p.info.variant.enum_name.span.sv));
                                    const SV msg = SV_from_str_len(r_msg, len);
                                    type_error(&source_info->source, &source_info->source_filename, &p.info.variant.enum_name.span.sv, &msg);
                                    exit(1);
                                }
                                bool variant_found = false;
                                size_t variant_nth = 0;
                                for (size_t k = 0; k < edef.variants.len; k++) {
                                    if (edef.variants.array[k].name.identifier == p.info.variant.variant_name.identifier) {
                                        variant_found = true;
                                        variant_nth = k;
                                        break;
                                    }
                                }
                                if (!variant_found) {
                                    char r_msg[256] = {0};
                                    int len = snprintf(r_msg, sizeof(r_msg), "unknown variant '"SV_format"' of enum '"SV_format"'", SV_v_args(p.info.variant.variant_name.span.sv) ,SV_v_args(edef.name.span.sv));
                                    const SV msg = SV_from_str_len(r_msg, len);
                                    type_error(&source_info->source, &source_info->source_filename, &p.info.variant.enum_name.span.sv, &msg);
                                    exit(1);
                                }
                                bool variant_fully_covered = true;
                                for (size_t sub = 0; sub < p.info.variant.sub_patterns.len; sub++) {
                                    Pattern sub_p = p.info.variant.sub_patterns.array[sub];
                                    if (sub_p.kind != PAT_WILDCARD && sub_p.kind != PAT_BINDING) {
                                        variant_fully_covered = false; // e.g., a literal like '3' is refutable
                                        break;
                                    }
                                }

                                if (variant_fully_covered) {
                                    covered[variant_nth] = true;
                                }
                            }
                        }

                        // Verify exhaustiveness
                        if (!has_global_wildcard) {
                            for (size_t k = 0; k < edef.variants.len; k++) {
                                if (!covered[k]) {
                                    // TokenSpan error_span = instr.info.match_meta__match.patterns.array[0].span;

                                    char r_msg[256];
                                    int len = snprintf(r_msg, sizeof(r_msg), "non-exhaustive match: missing variant '"SV_format"' (or refutable sub-pattern)", SV_v_args(edef.variants.array[k].name.span.sv));
                                    const SV msg = SV_from_str_len(r_msg, len);

                                    type_error(&source_info->source, &source_info->source_filename, &fn.error_info_list.array[instr.error_info_index].info.match_meta__match.match_kw.sv,&msg);
                                    free(covered);
                                    exit(1);
                                }
                            }
                        }
                        free(covered);
                    } else if (target_t.ty == TC_TY_Primitive) {
                        bool has_wildcard_or_bind = false;
                        for (size_t n = 0; n < instr.info.match_meta__match.patterns.len; n++) {
                            const Pattern p = instr.info.match_meta__match.patterns.array[n];
                            if (p.kind == PAT_BINDING || p.kind == PAT_WILDCARD) {
                                has_wildcard_or_bind = true;
                                break;
                            }
                        }
                        if (!has_wildcard_or_bind) {
                            const SV msg = SV_from_str_len("non-exhaustive match: missing binding or wildcard (required for number matches)", 79);
                            type_error(&source_info->source, &source_info->source_filename, &fn.error_info_list.array[instr.error_info_index].info.match_meta__match.match_kw.sv,&msg);
                            exit(1);
                        }
                    } else {
                        INTERNAL_ERROR("should be checked at this point to be either enum or number")
                    }
                    break;
                }
                case ENUM_GET_FIELD_PTR: {
                    const TC_typeid et = temporals_map->array[instr.info.enum_get_field_ptr.enum_src.val].type;

                    // const IR_temporal_and_TC_type et_t = perf_auto_deref(&et, &instr.info.enum_value_extract.enum_src, &highest_temporal, temporals_map);
                    if (et.ty != TC_TY_Ref) {
                        INTERNAL_ERROR()
                    }

                    const TC_typeid et_t_type = *et.info.contained;

                    if (et_t_type.ty != TC_TY_Enum) {
                        const SV et_sv = format_TC_typeid(&et_t_type, identifier_map, nct);
                        char r_msg[256] = {0};
                        int len = snprintf(r_msg, sizeof(r_msg), "expected enum type but got "C_BOLD""SV_format""C_RESET"", SV_v_args(et_sv));
                        const SV msg = SV_from_str_len(r_msg, len);
                        type_error(&source_info->source, &source_info->source_filename, &fn.error_info_list.array[instr.error_info_index].info.enum_get_field_ptr.whole.sv, &msg);
                        exit(1);
                    }
                    const TC_enumdef edef = nct->enums.array[et_t_type.info.type_id];
                    if (edef.name.identifier != instr.info.enum_get_field_ptr.enum_name.identifier) {
                        char r_msg[256] = {0};
                        int len = snprintf(r_msg, sizeof(r_msg), "expected enum '"SV_format"' but found '"SV_format"'", SV_v_args(edef.name.span.sv), SV_v_args(instr.info.enum_get_field_ptr.enum_name.span.sv));
                        const SV msg = SV_from_str_len(r_msg, len);
                        type_error(&source_info->source, &source_info->source_filename, &instr.info.enum_get_field_ptr.enum_name.span.sv, &msg);
                        exit(1);
                    }

                    bool found_v = false;
                    size_t v_nth = 0;
                    for (size_t n = 0; n < edef.variants.len; n++) {
                        if (edef.variants.array[n].name.identifier == instr.info.enum_get_field_ptr.variant_name.identifier) {
                            found_v = true;
                            v_nth = n;
                            break;
                        }
                    }
                    if (!found_v) {
                        const SV msg = SV_from_str_len("unknown enum variant", 20);
                        type_error(&source_info->source, &source_info->source_filename, &instr.info.enum_get_field_ptr.variant_name.span.sv, &msg);
                        exit(1);
                    }
                    if (instr.info.enum_get_field_ptr.value_nth >= edef.variants.array[v_nth].values_len) {
                        char r_msg[256] = {0};
                        int len = snprintf(r_msg, sizeof(r_msg), "variant '"SV_format"' of enum '"SV_format" has only %lu values packed but %u-nth value was accessed",
                            SV_v_args(instr.info.enum_get_field_ptr.variant_name.span.sv),
                            SV_v_args(instr.info.enum_get_field_ptr.enum_name.span.sv),
                            edef.variants.array[v_nth].values_len,
                            instr.info.enum_get_field_ptr.value_nth + 1
                        );
                        const SV msg = SV_from_str_len(r_msg, len);
                        type_error(&source_info->source, &source_info->source_filename, &fn.error_info_list.array[instr.error_info_index].info.enum_get_field_ptr.whole.sv, &msg);
                        exit(1);
                    }

                    // const TC_typeid extracted_val_t = edef.variants.array[v_nth].values[instr.info.enum_get_field_ptr.value_nth];
                    TC_typeid* extracted_val_t_r = malloc(sizeof(TC_typeid));
                    *extracted_val_t_r = edef.variants.array[v_nth].values[instr.info.enum_get_field_ptr.value_nth];
                    const TC_typeid extracted_val_t = (TC_typeid) {
                        .ty = TC_TY_Ref,
                        .info.contained = extracted_val_t_r,
                    };
                    const TC_typeid should_be_t = temporals_map->array[instr.info.enum_get_field_ptr.dest.val].type;

                    if (!is_type_eq(&extracted_val_t, &should_be_t)) {
                        const SV t1_sv = format_TC_typeid(&extracted_val_t, identifier_map, nct);
                        const SV t2_sv = format_TC_typeid(&should_be_t, identifier_map, nct);
                        char r_msg[256] = {0};
                        int len = snprintf(r_msg, sizeof(r_msg), "expected type "C_BOLD""SV_format""C_RESET" but found "C_BOLD""SV_format""C_RESET"", SV_v_args(t2_sv), SV_v_args(t1_sv));
                        const SV msg = SV_from_str_len(r_msg, len);
                        type_error(&source_info->source, &source_info->source_filename, &fn.error_info_list.array[instr.error_info_index].info.enum_get_field_ptr.whole.sv, &msg);
                        exit(1);
                    }

                    uint64_t val_offset = (edef.discriminant_size + edef.align - 1) & ~(edef.align - 1);
                    for (size_t n = 0; n < instr.info.enum_get_field_ptr.value_nth; n++) {
                        const SizeAlign sz = calc_size_of(&edef.variants.array[v_nth].values[n],nct, target_info);
                        val_offset = (val_offset + sz.align - 1) & ~(sz.align - 1);
                        val_offset += sz.size;
                    }

                    const SizeAlign sz = calc_size_of(extracted_val_t_r, nct, target_info);
                    val_offset = (val_offset + sz.align - 1) & ~(sz.align - 1);


                    printf("   %%%lu = CALC_PTR (%%%lu + %lu) {read size = %lu bytes}\n",
                        instr.info.enum_get_field_ptr.dest.val,
                        instr.info.enum_get_field_ptr.enum_src.val,
                        val_offset,
                        sz.size
                    );

                    // printf("   %%%lu = ENUM_VALUE_EXTRACT from %%%lu at offset %lu {.value size=%lu bytes}\n",
                    //     instr.info.enum_value_extract.dest.val,
                    //     et_t.temporal.val,
                    //     val_offset,
                    //     sz.size
                    // );

                    break;
                }
                case IR_INST_TY__ENUM_VARIANT_NUMBER_EXTRACT: {
                    const TC_typeid enum_t = temporals_map->array[instr.info.enum_variant_number_extract.enum_src.val].type;
                    // const IR_temporal_and_TC_type au_t = perf_auto_deref(&enum_t, &instr.info.enum_variant_number_extract.enum_src, &highest_temporal, temporals_map);

                    if (enum_t.ty != TC_TY_Ref) {
                        INTERNAL_ERROR()
                    }

                    const TC_typeid au_t_type = *enum_t.info.contained;

                    if (au_t_type.ty != TC_TY_Enum) {
                        // type_error(&source_info->source, &source_info->source_filename);
                        exit(1);
                    }
                    const TC_enumdef edef = nct->enums.array[au_t_type.info.type_id];
                    // printf("   %%%lu = ENUM_VARIANT_NUMBER EXTRACT from %%%lu {.variant number size=%u bytes}\n", instr.info.enum_variant_number_extract.dest.val, au_t.temporal.val, edef.discriminant_size);
                    printf("   %%%lu = READ_PTR from (%%%lu + %u) {read size = %u bytes}\n",
                        instr.info.enum_variant_number_extract.dest.val,
                        instr.info.enum_variant_number_extract.enum_src.val,
                        0u,
                        edef.discriminant_size
                    );
                    break;
                }
                case IR_INST_TY__ENUM_VARIANT_NUMBER_FROM_TYPE: {
                    INTERNAL_ERROR("should have been replaced by CONST_INT at type-inference pass")
                    break;
                }
            }
        }

        const MIR_block mir_block = (MIR_block){
            .instructions = mir_instrs
        };
        MIR_block_LIST_push(&mir_blocks, mir_block);
    }

    return (MIR_func){
        .fn_name = fn.fn_name,
        .blocks = mir_blocks,
        .highest_temporal_number = highest_temporal,
        .temporals = mir_temporals
    };
}

typedef struct {
    IR_temporal from;
    IR_temporal to;
} IR_temp_map;

init_prefixed_list_type(IR_temp_map)

void variable_storage_promotion_pass(
    const IR_func fn,
    khash_t(sv_u64_map)* restrict identifier_map,
    const TC_Entry_LIST temporals,
    IR_temporal_LIST* restrict promote_to_stack_temps,
    const TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info
) {
    /*

    // Phase 1: Instruction scan — find temporals that must be addressable
    for each block in fn.blocks (forward scan):
        for each instruction in block.instructions:
            match instruction.type:
                VAR_DECL:
                    temporals[instr.dest].is_addressable = true
                REF_OF:
                    temporals[instr.src].is_addressable = true
                ASSIGN:
                    temporals[instr.dest].is_addressable = true
                MATCH_META__TARGET:
                    temporals[instr.target].is_addressable = true

    // Phase 2: Size-based promotion (large values can't fit in registers)
    for each temporal n in 1..highest_temporal_number:
        if !temporals[n].is_addressable:
            sz = path_size_align_for_materialized_type(temporals[n].type, ...)
            if sz.size > target_info->ptr_size:
                temporals[n].is_addressable = true  // Rule R3

    // Phase 3: Destructor-based promotion (RAII needs stable address)
    for each temporal n in 1..highest_temporal_number:
        if !temporals[n].is_addressable:
            if temporals[n].type is in type_needing_destructor:
                temporals[n].is_addressable = true  // Rule R7


     */
    
    IR_temp_map_LIST temp_map = IR_temp_map_LIST_new();
    
    for (ptrdiff_t block_id = 0; block_id < (ptrdiff_t) fn.blocks.len; block_id++) {
        const IR_block block = fn.blocks.array[block_id];
        for (size_t instr_nth = 0; instr_nth < block.instructions.len; instr_nth++) {
            const IR_instr instr = block.instructions.array[instr_nth];
            switch (instr.ty) {
                case IR_INST_TY__MOVE: {
                    // check because of auto-ref
                    const TC_typeid dest_ty = temporals.array[instr.info.move.dest.val].type;
                    const TC_typeid src_ty = temporals.array[instr.info.move.src.val].type;
                    if (dest_ty.ty != TC_TY_Ref) {
                        break;
                    }
                    if (is_type_eq(&dest_ty, &src_ty)) {
                        break;
                    }
                    if (src_ty.ty != TC_TY_Ref) {
                        /// Ref<T> <- T
                        IR_temporal_LIST_push(promote_to_stack_temps, instr.info.move.src);
                        break;
                    }
                    // try to peel away simultaniously both to unpack the relationship
                    TC_typeid d_ty = *dest_ty.info.contained;
                    TC_typeid s_ty = *src_ty.info.contained;
                    while(d_ty.ty == TC_TY_Ref && s_ty.ty == TC_TY_Ref) {
                        d_ty = *d_ty.info.contained;
                        s_ty = *s_ty.info.contained;
                    }
                    if(d_ty.ty == TC_TY_Ref && s_ty.ty != TC_TY_Ref) {
                        IR_temporal_LIST_push(promote_to_stack_temps, instr.info.move.src);
                        break;
                    }
                    
                    // nope, do not promote
                    
                    break;
                }
                case IR_INST_TY__REF_OF: {
                    IR_temporal_LIST_push(promote_to_stack_temps, instr.info.ref_of.src);
                    break;
                }
                case IR_INST_TY__VAR_DECL: {
                    const SizeAlign sz = calc_size_of(&temporals.array[instr.info.var_decl.dest.val].type, nct, target_info);
                    if (sz.size > target_info->reg_size) {
                        IR_temporal_LIST_push(promote_to_stack_temps, instr.info.var_decl.dest);
                    }
                    break;
                }
                case IR_INST_TY__ASSIGN: {
                    // make a map record so that if %dest was determind for promotion, push it to %old
                    IR_temp_map_LIST_push(&temp_map, (IR_temp_map){
                        instr.info.assign.dest,
                        instr.info.assign.old
                    });
                    break;
                }
                case IR_INST_TY__MATCH_META__TARGET: {
                    IR_temporal_LIST_push(promote_to_stack_temps, instr.info.match_meta__target.target);
                    break;
                }
                case IR_INST_TY__STORE_PTR:
                    break;
                case IR_INST_TY__GET_STRUCT_FIELD_PTR:
                    break;
                case IR_INST_TY__INDEX_PTR:
                    break;
                case IR_INST_TY__READ_PTR:
                    break;
                case IR_INST_TY__MATCH_META__MATCH:
                    break;
                case ENUM_GET_FIELD_PTR:
                    break;
                case IR_INST_TY__INVALID:
                case IR_INST_TY__PHI:
                case IR_INST_TY__DROP:
                case IR_INST_TY__EXPLICIT_CAST:
                case IR_INST_TY__CONST_INT:
                case IR_INST_TY__CONST_FLOAT:
                case IR_INST_TY__CONST_STRING:
                case IR_INST_TY__MAKE_ARRAY:
                case IR_INST_TY__OP_ADD:
                case IR_INST_TY__OP_SUB:
                case IR_INST_TY__OP_DIV:
                case IR_INST_TY__OP_MUL:
                case IR_INST_TY__OP_POW:
                case IR_INST_TY__OP_OR:
                case IR_INST_TY__OP_AND:
                case IR_INST_TY__OP_CMP_EQ:
                case IR_INST_TY__OP_CMP_NOT_EQ:
                case IR_INST_TY__OP_CMP_LESS:
                case IR_INST_TY__OP_CMP_LESS_EQ:
                case IR_INST_TY__OP_CMP_GREATER:
                case IR_INST_TY__OP_CMP_GREATER_EQ:
                case IR_INST_TY__BRANCH:
                case IR_INST_TY__JUMP:
                case IR_INST_TY__RETURN_VOID:
                case IR_INST_TY__RETURN:
                case IR_INST_TY__FN_CALL_RETVOID:
                case IR_INST_TY__FN_CALL_RETVAL:
                case IR_INST_TY__MAKE_STRUCT:
                case IR_INST_TY__MAKE_ENUM:
                case IR_INST_TY__ENUM_VARIANT_NUMBER_EXTRACT:
                case IR_INST_TY__ENUM_VARIANT_NUMBER_FROM_TYPE:
                    break;
            }
        }
    }
    
    // printf("--- temp_map --- \n");
    // for(size_t n = 0; n < temp_map.len; n++) {
    //     printf("  %%%lu -> %%%lu\n", temp_map.array[n].from.val, temp_map.array[n].to.val);
    // }
    while(1) {
        bool had_new = false;
        for(size_t n = 0; n < temp_map.len; n++) {
            bool found_from = false;
            bool found_to = false;
            for(size_t p = 0; p < promote_to_stack_temps->len; p++) {
                if (promote_to_stack_temps->array[p].val == temp_map.array[n].from.val) {
                    found_from = true;
                }
                if (promote_to_stack_temps->array[p].val == temp_map.array[n].to.val) {
                    found_to = true;
                }
                if(found_from && found_to) break;
            }
            if(found_from && !found_to) {
                had_new = true;
                IR_temporal_LIST_push(promote_to_stack_temps, temp_map.array[n].to);
            }
        }
        if(!had_new) {
            break;
        }
    }

//     UNIMPLEMENTED("hey")
}

MIR_func TC_process_fn(
    const IR_func fn,
    khash_t(sv_u64_map)* restrict identifier_map,
    const IR_func_LIST* restrict functions,
    const CFFI_IR_extern_fn_LIST* restrict external_functions,
    TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info,
    TC_typeid_LIST* restrict type_needing_destructor
) {
    {
        const SV fn_name = fn.fn_name.span.sv;
        printf("TYPECHECKING FN '"SV_format"' {raw='"SV_format"'}\n", SV_v_args(fn_name), SV_v_args(fn.raw_name));
    }

    TC_Entry_LIST temporals;

    type_inference_pass_over_fn(fn, identifier_map, functions, external_functions, nct, target_info, source_info,
                                &temporals);

    for (size_t t = 1; t < temporals.len; t++) {
        const TC_Entry e = temporals.array[t];
        if (!e.known_temporal) {
            printf("  %%%lu UNKNOWN\n", t);
            continue;
        }
        if (e.type.ty == TC_TY_Void) {
            printf("ERROR: VOID TYPE %%%lu\n", t);
            continue;
        }
        printf("   %%%lu = {", t);
        print_TC_typeid(&e.type, identifier_map, nct);
        printf("}, {"SV_format"}\n", SV_v_args(e.type.full_typeuse_span.sv));
    }

    /// determine if variable is no longer a pure temporal, but ALLOC->PTR
    IR_temporal_LIST promote_to_stack_temps = IR_temporal_LIST_new();

    variable_storage_promotion_pass(fn, identifier_map, temporals, &promote_to_stack_temps, nct, target_info);

    printf("------- promote_to_stack_temps ---- \n");
    for(size_t n = 0; n < promote_to_stack_temps.len; n++) {
        printf("  %%%lu\n", promote_to_stack_temps.array[n].val);
    }
    
//     UNIMPLEMENTED("variable storage promotion analys pass");

    // todo: pass `promote_to_stack_temps` into lowering_and_emission_pass_over_fn
    const MIR_func mir_fn = lowering_and_emission_pass_over_fn(fn, identifier_map, functions, external_functions,
                                                               nct, target_info, source_info, &temporals,
                                                               promote_to_stack_temps,
                                                               type_needing_destructor);

    TC_Entry_LIST_free(&temporals);
    return mir_fn;
}

/// typechecking pass (two passes) + lowering to MIR
/// performs Bidirectional Typechecking
void TC_typecheck_and_lower(
    khash_t(sv_u64_map)* restrict identifier_map,
    const IR_func_LIST* restrict functions,
    const CFFI_IR_extern_fn_LIST* restrict external_functions,
    TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info,
    TC_typeid_LIST* restrict type_needing_destructor
) {
    printf("(fist type is from type_id, second from type's span)\n");
    for (size_t n = 0; n < functions->len; n++) {
        const IR_func fn = functions->array[n];
        TC_process_fn(fn, identifier_map,
                      functions,
                      external_functions,
                      nct,
                      target_info,
                      source_info,
                      type_needing_destructor
        );
    }
    for (size_t n = 0; n < nct->materialized_generic_functions.len; n++) {
        const IR_func fn = nct->materialized_generic_functions.array[n].fn;
        TC_process_fn(fn, identifier_map,
                      functions,
                      external_functions,
                      nct,
                      target_info,
                      source_info,
                      type_needing_destructor
        );
    }
}
