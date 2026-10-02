#include "typechecking.h"

#include <stddef.h>

#include "error.h"
#include "utils/dev.h"
#include "utils/color.h"


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
                const SV msg2 = SV_from_str("String as type does not exist, you can use Slice<Char>, List<Char>, Array<Char, N> instead");
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

                bool args_match = false;
                for (size_t j = 0; j < tu.contained_len; j++) {
                    if (!CMP_IR_typeuse(&tu.contained[j], &type->contained[j])) {
                        args_match = true;
                        break;
                    }
                }
                if (args_match) {
                    continue;
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

/// on success returns TRUE, and CAST from source to dest can be emitted
bool try_to_cast_to(const TC_typeid* source, const TC_typeid* dest) {
    switch (source->ty) {
        case TC_TY_Primitive: {
            if (dest->ty != TC_TY_Primitive && dest->ty != TC_TY_Enum) return false;
            // dest->info.primitive;
            return true;
        }
        case TC_TY_Enum: {
            if (dest->ty != TC_TY_Primitive && dest->ty != TC_TY_Enum) return false;
            UNIMPLEMENTED("try_to_cast_to for TC_TY_Enum")
        }

        case TC_TY_Void:
        case TC_TY_Struct:
        case TC_TY_Ref:
        case TC_TY_Array:
        case TC_TY_List:
        case TC_TY_Box:
        case TC_TY_Slice:
        case TC_TY_External_Type:
            return false;
    }
    UNREACHABLE()
}

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
    // union {
    //     void* null;
    //     TC_Value value;
    // };
} TC_Entry;

init_prefixed_list_type(TC_Entry)


// returns new_temporal
/// TODO: fix
size_t TC_implicit_cast(
    const TC_typeid out_type,
    const TC_typeid in_type,
    const size_t in_temporal,
    const size_t block_id,
    const size_t instr_nth,
    const khash_t(sv_u64_map)* restrict identifier_map,
    const TC_TypeTable* restrict nct,
    TC_Entry_LIST* restrict temporals_map,
    const SourceInfo* restrict source_info,
    const IR_instr_error_info error_info
) {
    if (out_type.ty != TC_TY_Primitive) {
        // make into internal_error and prevent from being called
        fprintf(stderr, "[typ-error] in 'TC_implicit_cast' out_type is not primitive\n");
        exit(1);
    }

    if (in_type.ty != TC_TY_Primitive) {
        fprintf(stderr, "[typ-error] in 'TC_implicit_cast' in_type is not primitive\n");
        exit(1);
    }

    switch (out_type.info.primitive) {
        case TC_Primitive__Int8:
        case TC_Primitive__Int16:
        case TC_Primitive__Int32:
        case TC_Primitive__Int64: {
            switch (in_type.info.primitive) {
                case TC_Primitive__Int8:
                case TC_Primitive__Int16:
                case TC_Primitive__Int32:
                case TC_Primitive__Int64: {
                    if ((uint32_t) (out_type.info.primitive) < (uint32_t) (in_type.info.primitive)) {
                        // fprintf(stderr, "[type-error] cannot autocast, type narrowing not allowed, use explicit cast<T>()\n");
                        // exit(1);
                        goto cannot_use_explicit_cast;
                    }
                    break;
                }
                case TC_Primitive__Char:
                case TC_Primitive__Bool: {
                    // ok
                    break;
                }
                default: {
                    // X
                    goto cannot_use_explicit_cast;
                }
            }
            break;
        }
        case TC_Primitive__Uint8:
        case TC_Primitive__Uint16:
        case TC_Primitive__Uint32:
        case TC_Primitive__Uint64: {
            switch (in_type.info.primitive) {
                case TC_Primitive__Uint8:
                case TC_Primitive__Uint16:
                case TC_Primitive__Uint32:
                case TC_Primitive__Uint64: {
                    if ((uint32_t) (out_type.info.primitive) < (uint32_t) (in_type.info.primitive)) {
                        // fprintf(stderr, "[type-error] cannot autocast, type narrowing not allowed, use explicit cast<T>()\n");
                        // exit(1);
                        goto cannot_use_explicit_cast;
                    }
                    break;
                }
                case TC_Primitive__Char:
                case TC_Primitive__Bool: {
                    // ok
                    break;
                }
                default: {
                    goto cannot_use_explicit_cast;
                }
            }
            break;
        }
        case TC_Primitive__Float32:
        case TC_Primitive__Float64: {
            switch (in_type.info.primitive) {
                case TC_Primitive__Float32:
                case TC_Primitive__Float64: {
                    if ((uint32_t) (out_type.info.primitive) < (uint32_t) (in_type.info.primitive)) {
                        // fprintf(stderr, "[type-error] cannot autocast, type narrowing not allowed, use explicit cast<T>()\n");
                        // exit(1);
                        goto cannot_use_explicit_cast;
                    }
                    break;
                }
                default: {
                    goto cannot_use_explicit_cast;
                }
            }
            break;
        }
        case TC_Primitive__Char:
        case TC_Primitive__Bool: {
            goto cannot_use_explicit_cast;
        }
        default: {
            INTERNAL_ERROR()
        }
    }
    goto do_it;

cannot_use_explicit_cast: {
        if (error_info.kind != IR_IEI_math_operation) {
            INTERNAL_ERROR("expected implicit cast on math op")
        }
        // error_info.info.math_operation.
        char* s = malloc(sizeof(char) * 100);
        snprintf(s, 100, "expected value of type "C_BOLD""SV_format""C_RESET" but found "C_BOLD""SV_format""C_RESET"",
                 SV_v_args(in_type.full_typeuse_span.sv), SV_v_args(out_type.full_typeuse_span.sv));
        const SV msg = SV_from_str(s);
        syntax_error(&source_info->source, &source_info->source_filename, &error_info.info.math_operation.whole.sv,
                     &msg);
        const SV hint = SV_from_str(
            "try using "C_BOLD"cast<T>()"C_RESET" if you need to convert between primitive types");
        hint_standalone(&hint);
        free(s);
        exit(1);
    }

do_it:
    TC_Entry_LIST_push(temporals_map, (TC_Entry){
                           .known_temporal = true,
                           .type = out_type,
                           // .origin = (TC_InstrPos) { .block = block_id, .instr_nth = instr_nth },
                           .is_value_known = false,
                           // .null = NULL
                       });
    const size_t new_temporal = temporals_map->len - 1;

    NEW_INSTR_BEGIN
    printf("NEW_INSTR: %%%lu = IMPLICIT_CAST %%%lu {", new_temporal, in_temporal);
    print_TC_typeid(&in_type, identifier_map, nct);
    printf(" -> ");
    print_TC_typeid(&out_type, identifier_map, nct);
    printf("}\n");
    NEW_INSTR_END

    return new_temporal;
}

void RAII_emit_typed_destructor(
    const TC_typeid* restrict type,
    const size_t in_obj_ptr_temporal,
    const size_t block_id,
    const size_t instr_nth,
    const TC_TypeTable* restrict nct,
    TC_Entry_LIST* restrict temporals_map,
    TC_typeid_LIST* restrict type_needing_destructor,
    const khash_t(sv_u64_map)* restrict identifier_map
) {
    NEW_INSTR_BEGIN
    printf("NEW_INSTR: CALL_TYPE_DESTRUCTOR{");
    print_TC_typeid(type, identifier_map, nct);
    printf("} %%%lu\n", in_obj_ptr_temporal);
    NEW_INSTR_END

    TC_typeid_LIST_push(type_needing_destructor, *type);
}

void RAII_drop(
    const TC_typeid* restrict type,
    const size_t in_obj_ptr_temporal,
    const size_t block_id,
    const size_t instr_nth,
    const TC_TypeTable* restrict nct,
    TC_Entry_LIST* restrict temporals_map,
    TC_typeid_LIST* restrict type_needing_destructor,
    const khash_t(sv_u64_map)* restrict identifier_map
) {
    switch (type->ty) {
        case TC_TY_Void:
        case TC_TY_Primitive:
        case TC_TY_Ref:
        case TC_TY_Slice: {
            NEW_INSTR_BEGIN
            printf("NEW_INSTR: FREE_HEAP_PTR %%%lu\n", in_obj_ptr_temporal);
            NEW_INSTR_END
        }
        case TC_TY_External_Type: return;


        case TC_TY_Box: {
            // BOX is just an owned heap ptr
            RAII_drop(type->info.contained, in_obj_ptr_temporal, block_id, instr_nth, nct, temporals_map,
                      type_needing_destructor, identifier_map);
            break;
        }

        case TC_TY_List:
        case TC_TY_Struct:
        case TC_TY_Enum:
        case TC_TY_Array: {
            RAII_emit_typed_destructor(type, in_obj_ptr_temporal, block_id, instr_nth, nct, temporals_map,
                                       type_needing_destructor, identifier_map);
            break;
        }
    }
}



void type_inference_pass_over_fn(
    const IR_func fn,
    const khash_t(sv_u64_map)* restrict identifier_map,
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
                           });
    }

    while (temporals_map->len < fn.highest_temporal_number + 1) {
        TC_Entry_LIST_push(temporals_map, (TC_Entry){.known_temporal = false});
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

                case IR_INST_TY__CONST_INT: {
                    // set as INT32 if unknown temporal and make the pass repeat
                    if (!temporals_map->array[instr.info.const_int.dest.val].known_temporal) {
                        temporals_map->array[instr.info.const_int.dest.val] = (TC_Entry) {
                            .known_temporal = true,
                            .type = (TC_typeid) {
                                .ty = TC_TY_Primitive,
                                .info.primitive = TC_Primitive__Int32,
                                .full_typeuse_span = (TokenSpan) { .sv=SV_from_str_len("Int32", 5) }
                            },
                            .is_value_known = false
                        };
                        block_id = (ptrdiff_t)fn.blocks.len; // no -1, because of (block_id--) in the for loop
                        perf_jump = true;
                    }
                    break;
                }
                case IR_INST_TY__CONST_FLOAT: {
                    // set as FLOAT32 if unknown temporal and make the pass repeat
                    if (!temporals_map->array[instr.info.const_int.dest.val].known_temporal) {
                        temporals_map->array[instr.info.const_int.dest.val] = (TC_Entry) {
                            .known_temporal = true,
                            .type = (TC_typeid) {
                                .ty = TC_TY_Primitive,
                                .info.primitive = TC_Primitive__Float32,
                                .full_typeuse_span = (TokenSpan) { .sv=SV_from_str_len("Float32", 7) }
                            },
                            .is_value_known = false
                        };
                        block_id = (ptrdiff_t)fn.blocks.len; // no -1, because of (block_id--) in the for loop
                        perf_jump = true;
                    }
                    break;
                }
                case IR_INST_TY__CONST_STRING: {
                    if (!temporals_map->array[instr.info.const_int.dest.val].known_temporal) {
                        TC_typeid* inner = malloc(sizeof(TC_typeid));
                        *inner = (TC_typeid) {
                            .ty = TC_TY_Primitive,
                            .info.primitive = TC_Primitive__Char,
                            .full_typeuse_span = (TokenSpan) { .sv = SV_from_str_len("Char", 4) }
                        };
                        temporals_map->array[instr.info.const_int.dest.val] = (TC_Entry) {
                            .known_temporal = true,
                            .type = (TC_typeid) {
                                .ty = TC_TY_Slice,
                                .info.contained = inner,
                                .full_typeuse_span = (TokenSpan) { .sv=SV_from_str_len("Slice<Char>", 11) }
                            },
                            .is_value_known = false
                        };
                        block_id = (ptrdiff_t)fn.blocks.len; // no -1, because of (block_id--) in the for loop
                        perf_jump = true;
                    }
                    break;
                }
                case IR_INST_TY__VAR_DECL: { // should be known by now from DROP
                    if (!temporals_map->array[instr.info.var_decl.dest.val].known_temporal) {
                        WARN("VAR_DECL with declare unknown temporal")
                    }

                    break;
                }
                case IR_INST_TY__DROP: {
                    if (temporals_map->array[instr.info.drop.dest.val].known_temporal) break;

                    const TC_typeid type = resolve_tc_type(instr.info.drop.type, nct, target_info, source_info);

                    temporals_map->array[instr.info.drop.dest.val] = (TC_Entry){
                        .known_temporal = true,
                        .type = type,
                        .is_value_known = false
                    };
                    break;
                }
                case IR_INST_TY__MOVE: {
                    if (!temporals_map->array[instr.info.move.dest.val].known_temporal) break;

                    const TC_typeid dest_type = temporals_map->array[instr.info.move.dest.val].type;

                    if (!temporals_map->array[instr.info.move.src.val].known_temporal) {
                        temporals_map->array[instr.info.move.src.val] = (TC_Entry){
                            .known_temporal = true,
                            .type = dest_type,
                            .is_value_known = false,
                        };
                    }

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
                        temporals_map->array[instr.info.math_cmp_boolean_op.src1.val] = (TC_Entry) {
                            .known_temporal = true,
                            .type = dest.type,
                            .is_value_known = false,
                        };
                    }

                    if (!temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].known_temporal) {
                        temporals_map->array[instr.info.math_cmp_boolean_op.src2.val] = (TC_Entry) {
                            .known_temporal = true,
                            .type = dest.type,
                            .is_value_known = false,
                        };
                    }

                    break;
                }
                case IR_INST_TY__RETURN: {
                    if (!temporals_map->array[instr.info.return_value.val].known_temporal) {
                        temporals_map->array[instr.info.return_value.val] = (TC_Entry) {
                            .known_temporal = true,
                            .type = ret_t,
                            .is_value_known = false,
                        };
                    }

                    break;
                }
                case IR_INST_TY__OP_OR:
                case IR_INST_TY__OP_AND: {
                    // TODO: what to do here? Do i need <bool> <op> <bool> ???
                    if (!temporals_map->array[instr.info.math_cmp_boolean_op.dest.val].known_temporal) {
                        temporals_map->array[instr.info.math_cmp_boolean_op.dest.val] = (TC_Entry) {
                            .known_temporal = true,
                            .type = (TC_typeid) {
                                .ty = TC_TY_Primitive,
                                .info.primitive = TC_Primitive__Bool,
                                .full_typeuse_span = (TokenSpan){.sv=SV_from_str_len("Bool", 4)},
                            },
                            .is_value_known = false,
                        };
                    }

                    if (!temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].known_temporal) {
                        temporals_map->array[instr.info.math_cmp_boolean_op.src1.val] = (TC_Entry) {
                            .known_temporal = true,
                            .type = (TC_typeid) {
                                .ty = TC_TY_Primitive,
                                .info.primitive = TC_Primitive__Bool,
                                .full_typeuse_span = (TokenSpan){.sv=SV_from_str_len("Bool", 4)},
                            },
                            .is_value_known = false,
                        };
                    }

                    if (!temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].known_temporal) {
                        temporals_map->array[instr.info.math_cmp_boolean_op.src2.val] = (TC_Entry) {
                            .known_temporal = true,
                            .type = (TC_typeid) {
                                .ty = TC_TY_Primitive,
                                .info.primitive = TC_Primitive__Bool,
                                .full_typeuse_span = (TokenSpan){.sv=SV_from_str_len("Bool", 4)},
                            },
                            .is_value_known = false,
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
                        temporals_map->array[instr.info.math_cmp_boolean_op.dest.val] = (TC_Entry) {
                            .known_temporal = true,
                            .type = (TC_typeid) {
                                .ty = TC_TY_Primitive,
                                .info.primitive = TC_Primitive__Bool,
                                .full_typeuse_span = (TokenSpan){.sv=SV_from_str_len("Bool", 4)},
                            },
                            .is_value_known = false,
                        };
                    }

                    // if one of args has known type, apply it to another
                    if (temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].known_temporal && (!temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].known_temporal)) {
                        temporals_map->array[instr.info.math_cmp_boolean_op.src2.val] = (TC_Entry) {
                            .known_temporal = true,
                            .type = temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].type,
                            .is_value_known = false,
                        };
                    }


                    if (temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].known_temporal && (!temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].known_temporal)) {
                        temporals_map->array[instr.info.math_cmp_boolean_op.src1.val] = (TC_Entry) {
                            .known_temporal = true,
                            .type = temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].type,
                            .is_value_known = false,
                        };
                    }


                    break;
                }
                case IR_INST_TY__PHI: {
                    bool visited = false;
                    for (size_t n = 0; n < visited_phis.len; n++) {
                        if ((ptrdiff_t)(visited_phis.array[n].block) == block_id && visited_phis.array[n].instr_nth == instr_nth) {
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
                    for (size_t n = 0; n<phi_args->len; n++) {
                        const IR_temporal_block_phi_record arg = phi_args->array[n];
                        if (!temporals_map->array[arg.temp.val].known_temporal) {
                            temporals_map->array[arg.temp.val] = (TC_Entry) {
                                .known_temporal = true,
                                .type = dest.type,
                                .is_value_known = false,
                            };
                        }
                        if ((ptrdiff_t)(arg.block) > highest_block) {
                            highest_block = (ptrdiff_t)arg.block;
                        }
                    }

                    TC_InstrPos_LIST_push(&visited_phis, (TC_InstrPos) { .block = block_id, .instr_nth = instr_nth });

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
                    size_t unwrap_cnt = 0;
                    TC_typeid inner = dest_type;
                    while (inner.ty == TC_TY_Ref || inner.ty == TC_TY_Box || inner.ty == TC_TY_Array || inner.ty == TC_TY_List || inner.ty == TC_TY_Slice) {
                        switch (inner.ty) {
                            case TC_TY_Ref:
                            case TC_TY_Box:
                            case TC_TY_Slice:
                            case TC_TY_List: {
                                inner = *inner.info.contained;
                                unwrap_cnt++;
                                break;
                            }
                            case TC_TY_Array: {
                                inner = *inner.info.t_array.contained;
                                unwrap_cnt++;
                                break;
                            }
                            default: {break;}
                        }
                    }
                    if (unwrap_cnt > 0) {
                        for(size_t n=0; n < instr.info.make_array.items->len; n++) {
                            const IR_temporal item = instr.info.make_array.items->array[n];
                            if (!temporals_map->array[item.val].known_temporal) {
                                temporals_map->array[item.val] = (TC_Entry) {
                                    .known_temporal = true,
                                    .type = inner,
                                    .is_value_known = false,
                                };
                            }
                        }
                    }
                    break;
                }
                case IR_INST_TY__BRANCH: {
                    if (!temporals_map->array[instr.info.branch.cond.val].known_temporal) {
                        temporals_map->array[instr.info.branch.cond.val] = (TC_Entry) {
                            .known_temporal = true,
                            .type = (TC_typeid) {
                                .ty = TC_TY_Primitive,
                                .info.primitive = TC_Primitive__Bool,
                                .full_typeuse_span = (TokenSpan){.sv=SV_from_str_len("Bool", 4)},
                            },
                            .is_value_known = false,
                        };
                    }
                    break;
                }
                case IR_INST_TY__GET_STRUCT_FIELD_PTR: {
                    if (temporals_map->array[instr.info.get_struct_field_ptr.struct_ptr.val].known_temporal &&
                        !temporals_map->array[instr.info.get_struct_field_ptr.dest.val].known_temporal
                    ) {
                        const TC_typeid src_struct = temporals_map->array[instr.info.get_struct_field_ptr.struct_ptr.val].type;
                        TC_typeid inner_src = src_struct;
                        while (inner_src.ty == TC_TY_Ref || inner_src.ty == TC_TY_Ref) {
                            inner_src = *inner_src.info.contained;
                        }
                        if (inner_src.ty == TC_TY_Struct) {
                            const TC_structdef* s = &nct->structs.array[inner_src.info.type_id];
                            for (size_t n=0; n < s->fields.len; n++) {
                                if (s->fields.array[n].name.identifier == instr.info.get_struct_field_ptr.field_name.identifier) {
                                    TC_typeid* ref_inner = malloc(sizeof(TC_typeid));
                                    *ref_inner = s->fields.array[n].type;

                                    const size_t MAX_SIZE = 128;
                                    char* new_type_name = malloc(sizeof(char)*MAX_SIZE);
                                    const int out_len = snprintf(new_type_name, MAX_SIZE, "Ref<"SV_format">", SV_v_args(ref_inner->full_typeuse_span.sv));
                                    new_type_name = realloc(new_type_name, sizeof(char)*out_len);
                                    const SV new_type = SV_from_str_len(new_type_name, out_len);


                                    temporals_map->array[instr.info.get_struct_field_ptr.dest.val] = (TC_Entry){
                                        .known_temporal = true,
                                        .type = (TC_typeid) {
                                            .ty = TC_TY_Ref,
                                            .info.contained = ref_inner,
                                            .full_typeuse_span = (TokenSpan) {.sv = new_type}, //ref_inner->full_typeuse_span
                                        },
                                        .is_value_known = false,
                                    };
                                    break;
                                }
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
                        char* new_type_name = malloc(sizeof(char)*MAX_SIZE);
                        const int out_len = snprintf(new_type_name, MAX_SIZE, "Ref<"SV_format">", SV_v_args(dest.full_typeuse_span.sv));
                        new_type_name = realloc(new_type_name, sizeof(char)*out_len);
                        const SV new_type = SV_from_str_len(new_type_name, out_len);

                        temporals_map->array[instr.info.read_ptr.ptr.val] = (TC_Entry){
                            .known_temporal = true,
                            .type = (TC_typeid) {
                                .ty = TC_TY_Ref,
                                .info.contained = out,
                                .full_typeuse_span = (TokenSpan) {.sv = new_type}, //ref_inner->full_typeuse_span
                            },
                            .is_value_known = false,
                        };
                    } else if (temporals_map->array[instr.info.read_ptr.ptr.val].known_temporal &&
                        !temporals_map->array[instr.info.read_ptr.dest.val].known_temporal
                    ) {
                        TC_typeid inner = temporals_map->array[instr.info.read_ptr.ptr.val].type;
                        size_t unwrap_cnt = 0;
                        while (inner.ty == TC_TY_Ref || inner.ty == TC_TY_Box) {
                            unwrap_cnt++;
                            inner = *inner.info.contained;
                        }
                        if (unwrap_cnt > 0) {
                            temporals_map->array[instr.info.read_ptr.dest.val] = (TC_Entry) {
                                .known_temporal = true,
                                .type = inner,
                                .is_value_known = false
                            };
                        }
                    }
                    break;
                }
                case IR_INST_TY__MAKE_STRUCT: {
                    const TC_typeid type = resolve_tc_type(instr.info.make_struct.type,nct,target_info,source_info);
                    if (type.ty != TC_TY_Struct) break;

                    const TC_structdef* st = &nct->structs.array[type.info.type_id];
                    if (!temporals_map->array[instr.info.make_struct.dest.val].known_temporal) {
                        temporals_map->array[instr.info.make_struct.dest.val] = (TC_Entry){
                            .known_temporal = true,
                            .type = type,
                            .is_value_known = false
                        };
                    }

                    for (size_t n=0; n < instr.info.make_struct.args->len; n++) {
                        const IR_named_value arg = instr.info.make_struct.args->array[n];
                        if (temporals_map->array[arg.value.val].known_temporal) continue;
                        for (size_t k=0; k < st->fields.len; k++) {
                            if(st->fields.array[k].name.identifier == arg.name.identifier) {
                                temporals_map->array[arg.value.val] = (TC_Entry) {
                                    .known_temporal = true,
                                    .type = st->fields.array[k].type,
                                    .is_value_known = false
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
                    const IR_temporal_LIST* lk = instr.ty == IR_INST_TY__FN_CALL_RETVAL ? instr.info.fn_call_retval.args : instr.info.fn_call_retvoid.args;
                    const Identifier name = instr.ty == IR_INST_TY__FN_CALL_RETVAL ? instr.info.fn_call_retval.name.identifier : instr.info.fn_call_retvoid.name.identifier;

                    for (size_t n = 0; n < functions->len; n++) {
                        if (functions->array[n].fn_name.identifier == name) {
                            if (lk->len == functions->array[n].args.len) {
                                for (size_t k = 0; k < lk->len; k++) {
                                    if (!temporals_map->array[lk->array[k].val].known_temporal) {
                                        const TC_typeid type = resolve_tc_type(&functions->array[n].args.array[k].type, nct, target_info, source_info);
                                        temporals_map->array[lk->array[k].val] = (TC_Entry) {
                                            .known_temporal = true,
                                            .type = type,
                                            .is_value_known = false
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
                    ) { // dest unknown, array known
                        const TC_typeid array_ptr = temporals_map->array[instr.info.index_ptr.array_ptr.val].type;
                        TC_typeid inner_type = array_ptr;
                        bool run = true;
                        bool unpacked_array = false;
                        while (run) {
                            switch (inner_type.ty) {
                                case TC_TY_Box:
                                case TC_TY_Ref: {
                                    inner_type = *inner_type.info.contained;
                                    break;
                                }
                                case TC_TY_List:
                                case TC_TY_Slice: {
                                    inner_type = *inner_type.info.contained;
                                    unpacked_array = true;
                                    run = false;
                                    break;
                                }
                                case TC_TY_Array: {
                                    inner_type = *inner_type.info.t_array.contained;
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
                            temporals_map->array[instr.info.index_ptr.dest.val] = (TC_Entry) {
                                .known_temporal = true,
                                .type = inner_type,
                                .is_value_known = false
                            };
                        }
                    }
                    if (!temporals_map->array[instr.info.index_ptr.index_val.val].known_temporal) {
                        temporals_map->array[instr.info.index_ptr.index_val.val] = (TC_Entry) {
                            .known_temporal = true,
                            .type = (TC_typeid) {
                                .ty = TC_TY_Primitive,
                                .info.primitive = TC_Primitive__Uint32,
                                .full_typeuse_span = (TokenSpan) {.sv = SV_from_str_len("Uint32", 6)}
                            },
                            .is_value_known = false,
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
                            temporals_map->array[instr.info.store_ptr.value.val] = (TC_Entry) {
                                .known_temporal = true,
                                .type = inner_type,
                                .is_value_known = false
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
                    if (!temporals_map->array[instr.info.explicit_cast.dest.val].known_temporal) {
                        const TC_typeid t = resolve_tc_type(instr.info.explicit_cast.type, nct, target_info, source_info);
                        temporals_map->array[instr.info.explicit_cast.dest.val] = (TC_Entry) {
                            .known_temporal = true,
                            .type = t,
                            .is_value_known = false
                        };
                    }
                    break;
                }
                case IR_INST_TY__MAKE_ENUM: {
                    const TC_typeid type = resolve_tc_type(instr.info.make_enum.type,nct,target_info,source_info);
                    if (type.ty != TC_TY_Enum) break;



                    if (!temporals_map->array[instr.info.make_enum.dest.val].known_temporal) {
                        temporals_map->array[instr.info.make_enum.dest.val] = (TC_Entry){
                            .known_temporal = true,
                            .type = type,
                            .is_value_known = false
                        };
                    }

                    const TC_enumdef* en = &nct->enums.array[type.info.type_id];
                    for (size_t v=0; v < en->variants.len; v++) {
                        if (en->variants.array[v].name.identifier == instr.info.make_enum.variant && en->variants.array[v].values_len == instr.info.make_enum.args->len) {
                            for (size_t a = 0; a < instr.info.make_enum.args->len; a++) {
                                if (!temporals_map->array[instr.info.make_enum.args->array[a].val].known_temporal) {
                                    temporals_map->array[instr.info.make_enum.args->array[a].val] = (TC_Entry) {
                                        .known_temporal = true,
                                        .type = en->variants.array[v].values[a],
                                        .is_value_known = false
                                    };
                                }
                            }
                            break;
                        }
                    }
                    break;
                }
            }
        }
    }
    // todo: fix external fn calls (do not infer types for them)
}

typedef struct {
    IR_temporal temporal;
    TC_typeid type;
} IR_temporal_and_TC_type;

// while Ref<T>, unpack
IR_temporal_and_TC_type perf_auto_deref(const TC_typeid* type, const IR_temporal* intake, size_t* highest_temporal) {
    if (type->ty != TC_TY_Ref) {
        return (IR_temporal_and_TC_type){ .temporal = *intake, .type = *type };
    }
    TC_typeid t = *type;
    uint64_t te = intake->val;
    while (t.ty == TC_TY_Ref) {
        const uint64_t new_temporal = (*highest_temporal)++;
        printf("  %%%lu = READ_PTR %%%lu\n", new_temporal, te);
        te = new_temporal;
        t = *t.info.contained;
    }
    return (IR_temporal_and_TC_type){ .temporal = (IR_temporal){ .val = te }, .type = t};
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
                    return (TC_typeid) { .ty=TC_TY_Void };
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
                    return (TC_typeid) { .ty=TC_TY_Void };
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
                    return (TC_typeid) { .ty=TC_TY_Void };
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
                    return (TC_typeid) { .ty=TC_TY_Void };
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
                    return (TC_typeid) { .ty=TC_TY_Void };
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
                    return (TC_typeid) { .ty=TC_TY_Void };
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
                    return (TC_typeid) { .ty=TC_TY_Void };
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
                    return (TC_typeid) { .ty=TC_TY_Void };
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
                    return (TC_typeid) { .ty=TC_TY_Void };
                }
                case TC_Primitive__Float32:
                case TC_Primitive__Float64: {
                    // INVALID
                    return (TC_typeid) { .ty=TC_TY_Void };
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
                    return (TC_typeid) { .ty=TC_TY_Void };
                }
                case TC_Primitive__Float32:
                case TC_Primitive__Float64: {
                    // INVALID
                    return (TC_typeid) { .ty=TC_TY_Void };
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
                    return (TC_typeid) { .ty=TC_TY_Void };
                }
                case TC_Primitive__Float32:
                case TC_Primitive__Float64: {
                    // INVALID
                    return (TC_typeid) { .ty=TC_TY_Void };
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
                    return (TC_typeid) { .ty=TC_TY_Void };
                }
                case TC_Primitive__Float32:
                case TC_Primitive__Float64: {
                    // INVALID
                    return (TC_typeid) { .ty=TC_TY_Void };
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
                    return (TC_typeid) { .ty=TC_TY_Void };
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
                    return (TC_typeid) { .ty=TC_TY_Void };
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
                    return (TC_typeid) { .ty=TC_TY_Void };
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
                    return (TC_typeid) { .ty=TC_TY_Void };
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
                    return (TC_typeid) { .ty=TC_TY_Void };
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
    return (TC_typeid) { .ty=TC_TY_Void };
}

DoubleIR_temporal_and_TC_typeid perf_numeric_promotion(const IR_temporal* a, const TC_typeid* a_type, const IR_temporal* b, const TC_typeid* b_type, size_t* highest_temporal) {
    const IR_temporal_and_TC_type derefered_a = perf_auto_deref(a_type, a, highest_temporal);
    const IR_temporal_and_TC_type derefered_b = perf_auto_deref(b_type, b, highest_temporal);

    const TC_typeid target_op_type = get_common_numeric_type(derefered_a.type, derefered_b.type);

    IR_temporal new_a = derefered_a.temporal;
    IR_temporal new_b = derefered_b.temporal;

    if (derefered_a.type.info.primitive != target_op_type.info.primitive) {
        const uint64_t new_temporal = (*highest_temporal)++;
        printf("   %%%lu = CAST<"SV_format">(%%%lu)\n", new_temporal, SV_v_args(target_op_type.full_typeuse_span.sv), new_a.val);
        new_a.val = new_temporal;
    }

    if (derefered_b.type.info.primitive != target_op_type.info.primitive) {
        const uint64_t new_temporal = (*highest_temporal)++;
        printf("   %%%lu = CAST<"SV_format">(%%%lu)\n", new_temporal, SV_v_args(target_op_type.full_typeuse_span.sv), new_b.val);
        new_b.val = new_temporal;
    }

    return (DoubleIR_temporal_and_TC_typeid) {
        .a = new_a,
        .b = new_b,
        .t = target_op_type
    };

    /*
    Math is special. You don't just want to match types; you want to widen them safely (e.g., Uint8 to Int32, or Int to Float).
    When to use it: OP_ADD, OP_SUB, OP_MUL, OP_CMP_LESS, etc.
    What it does: It takes two temporals. It calls perf_auto_deref on both first (so Ref<Int> becomes Int). Then, it checks their sizes, decides the common target type, and emits CAST instructions for whichever temporal is smaller.
     */
}

// The Condition Coercer
IR_temporal perf_bool(const IR_temporal* intake, const TC_typeid* intake_type, size_t* highest_temporal) {
    const IR_temporal_and_TC_type derefd = perf_auto_deref(intake_type, intake, highest_temporal);
    if (derefd.type.ty == TC_TY_Primitive && derefd.type.info.primitive == TC_Primitive__Bool) {
        return derefd.temporal;
    }
    if (derefd.type.ty == TC_TY_Primitive) {
        // emit check not zero
        // OP_CMP_NOT_EQ
        const uint64_t new_temporal = (*highest_temporal)++;
        printf("   %%%lu = OP_CMP_NOT_EQ %%%lu, 0\n", new_temporal, derefd.temporal.val);
        return (IR_temporal) { .val = new_temporal};
    }

    INTERNAL_ERROR("typechecking error here....");
}


IR_temporal_and_TC_type perf_auto_ref(const IR_temporal* intake, const TC_typeid* intake_type, size_t* highest_temporal) {
    const uint64_t new_temporal = (*highest_temporal)++;
    printf("   %%%lu = REF_OF %%%lu\n", intake->val, new_temporal);

    return (IR_temporal_and_TC_type){ .temporal =  (IR_temporal){.val=new_temporal},.type = (TC_typeid) {
        .ty = TC_TY_Ref,
        .info.contained = (TC_typeid*)intake_type,
        .full_typeuse_span = (TokenSpan) { .sv=intake_type->full_typeuse_span.sv }
    }};
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


bool is_type_eq(const TC_typeid* a, const TC_typeid* b) {
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

/// returns TRUE when possible
/// describes unintentional casting originating from two types not being equal (like Int32 a = 5_i16)
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
                return to->info.primitive >= TC_Primitive__Uint8 && to->info.primitive <= TC_Primitive__Uint64;
            }
            case TC_Primitive__Uint16: {
                return to->info.primitive >= TC_Primitive__Uint16 && to->info.primitive <= TC_Primitive__Uint64;
            }
            case TC_Primitive__Uint32: {
                return to->info.primitive >= TC_Primitive__Uint32 && to->info.primitive <= TC_Primitive__Uint64;
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
                    (to->info.primitive >= TC_Primitive__Uint8 && to->info.primitive <= TC_Primitive__Uint64)
                ;
            }
        }
    }
    UNIMPLEMENTED()
}

void lowering_and_emission_pass_over_fn(
    const IR_func fn,
    const khash_t(sv_u64_map)* restrict identifier_map,
    const IR_func_LIST* restrict functions,
    const CFFI_IR_extern_fn_LIST* restrict external_functions,
    TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info,
    const TC_Entry_LIST* restrict temporals_map,
    TC_typeid_LIST* restrict type_needing_destructor
) {

    printf("LINEAR TYPECHECK AND LOWERING:\n");
    size_t highest_temporal = fn.highest_temporal_number+1;

    for (ptrdiff_t block_id = 0 ; block_id < (ptrdiff_t) fn.blocks.len; block_id++) {
        const IR_block block = fn.blocks.array[block_id];
        for (size_t instr_nth = 0; instr_nth < block.instructions.len; instr_nth++) {
            const IR_instr instr = block.instructions.array[instr_nth];

            switch (instr.ty) {
                case IR_INST_TY__INVALID:
                    break;
                case IR_INST_TY__PHI:
                    break;
                case IR_INST_TY__DROP:
                    break;
                case IR_INST_TY__MOVE:
                    break;
                case IR_INST_TY__VAR_DECL:
                    break;
                case IR_INST_TY__STORE_PTR:
                    break;
                case IR_INST_TY__EXPLICIT_CAST: {
                    // can_be_explicit_casted
                    const TC_typeid src = temporals_map->array[instr.info.explicit_cast.src.val].type;
                    const TC_typeid cast_t = resolve_tc_type(instr.info.explicit_cast.type, nct, target_info, source_info);
                    if (!can_be_explicit_casted(&src, &cast_t)) {
                        const SV whole = fn.error_info_list.array[instr.error_info_index].info.explicit_cast.whole.sv;
                        const SV expr = fn.error_info_list.array[instr.error_info_index].info.explicit_cast.expr.sv;
                        const SV type = fn.error_info_list.array[instr.error_info_index].info.explicit_cast.type.sv;

                        const size_t MAX_SIZE = 1024;
                        char* r_msg = malloc(sizeof(char)*MAX_SIZE);
                        int len = snprintf(r_msg, MAX_SIZE, "cannot cast "C_BOLD""SV_format""C_RESET" into "C_BOLD""SV_format""C_RESET"", SV_v_args(src.full_typeuse_span.sv), SV_v_args(type));
                        const SV msg = SV_from_str_len(r_msg, len);
                        type_error(&source_info->source, &source_info->source_filename, &expr, &msg);
                        exit(1);

                    }
                    const TC_typeid dest_t = temporals_map->array[instr.info.explicit_cast.dest.val].type;

                    uint64_t out_temporal = instr.info.explicit_cast.dest.val;
                    if (!is_type_eq(&dest_t, &cast_t)) {
                        out_temporal = highest_temporal++;
                        if (!can_be_implicit_casted(&cast_t, &dest_t)) {
                            const SV whole = fn.error_info_list.array[instr.error_info_index].info.explicit_cast.whole.sv;
                            const SV expr = fn.error_info_list.array[instr.error_info_index].info.explicit_cast.expr.sv;
                            const SV type = fn.error_info_list.array[instr.error_info_index].info.explicit_cast.type.sv;

                            // expected <T> but found <T>
                            const size_t MAX_SIZE = 1024;
                            char* r_msg = malloc(sizeof(char)*MAX_SIZE);
                            int len = snprintf(r_msg, MAX_SIZE, "expected "C_BOLD""SV_format""C_RESET" but found "C_BOLD""SV_format""C_RESET"", SV_v_args(dest_t.full_typeuse_span.sv), SV_v_args(type));
                            const SV msg = SV_from_str_len(r_msg, len);
                            type_error(&source_info->source, &source_info->source_filename, &whole, &msg);
                            exit(1);

                            exit(1);
                        } else {
                            printf("   [D] implicit type conversion from ");
                            print_TC_typeid((const TC_typeid*)instr.info.explicit_cast.type, identifier_map, nct);
                            printf(" to ");
                            print_TC_typeid(&dest_t, identifier_map, nct);
                            printf(" is possible \n");
                        }
                    }
                    printf("   %%%lu = CAST<", out_temporal);
                    print_TC_typeid((const TC_typeid*)instr.info.explicit_cast.type, identifier_map, nct);
                    printf(">(%%%lu) [EXPLICIT]\n", instr.info.explicit_cast.src.val);

                    if (out_temporal != instr.info.explicit_cast.dest.val) {
                        printf("   %%%lu = CAST<", instr.info.explicit_cast.dest.val);
                        print_TC_typeid(&dest_t, identifier_map, nct);
                        printf(">(%%%lu) [IMPLICIT]\n", out_temporal);
                    }
                    break;
                }
                case IR_INST_TY__CONST_INT:
                    break;
                case IR_INST_TY__CONST_FLOAT:
                    break;
                case IR_INST_TY__CONST_STRING:
                    break;
                case IR_INST_TY__MAKE_ARRAY:
                    break;
                case IR_INST_TY__OP_ADD:
                case IR_INST_TY__OP_SUB:
                case IR_INST_TY__OP_DIV:
                case IR_INST_TY__OP_MUL:
                case IR_INST_TY__OP_POW: {
                    const DoubleIR_temporal_and_TC_typeid res = perf_numeric_promotion(
                        &instr.info.math_cmp_boolean_op.src1,
                        &temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].type,
                        &instr.info.math_cmp_boolean_op.src2,
                        &temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].type,
                        &highest_temporal
                    );

                    if (res.t.ty == TC_TY_Void) {
                        INTERNAL_ERROR("TYPE ERROR HERE");
                    }

                    uint64_t out_temporal = instr.info.math_cmp_boolean_op.dest.val;
                    if (
                        temporals_map->array[instr.info.math_cmp_boolean_op.dest.val].type.ty != TC_TY_Primitive ||
                        temporals_map->array[instr.info.math_cmp_boolean_op.dest.val].type.info.primitive != res.t.info.primitive
                    ) {
                        out_temporal = highest_temporal++;
                        if (!can_be_implicit_casted(&res.t, &temporals_map->array[instr.info.math_cmp_boolean_op.dest.val].type)) {
                            INTERNAL_ERROR("TYPE ERROR HERE");
                        } else {
                            printf("   [D] implicit type conversion from ");
                            print_TC_typeid(&res.t, identifier_map, nct);
                            printf(" to ");
                            print_TC_typeid(&temporals_map->array[instr.info.math_cmp_boolean_op.dest.val].type, identifier_map, nct);
                            printf(" is possible \n");
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
                    printf(" <");
                    print_TC_typeid(&res.t, identifier_map, nct);
                    printf("> %%%lu, %%%lu\n", res.a.val, res.b.val);

                    if (out_temporal != instr.info.math_cmp_boolean_op.dest.val) {
                        printf("   %%%lu = CAST<", instr.info.math_cmp_boolean_op.dest.val);
                        print_TC_typeid(&temporals_map->array[instr.info.math_cmp_boolean_op.dest.val].type, identifier_map, nct);
                        printf(">(%%%lu) [IMPLICIT]\n", out_temporal);
                    }
                    break;
                }
                case IR_INST_TY__OP_OR:
                case IR_INST_TY__OP_AND: {
                    const TC_typeid src1_t = temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].type;
                    const TC_typeid src2_t = temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].type;
                    const TC_typeid dest_t = temporals_map->array[instr.info.math_cmp_boolean_op.dest.val].type;

                    const IR_temporal src1_it = perf_bool(&instr.info.math_cmp_boolean_op.src1, &src1_t, &highest_temporal);
                    const IR_temporal src2_it = perf_bool(&instr.info.math_cmp_boolean_op.src2, &src2_t, &highest_temporal);

                    const TC_typeid bool_t = (TC_typeid) {
                        .ty = TC_TY_Primitive,
                        .info.primitive = TC_Primitive__Bool,
                        .full_typeuse_span = (TokenSpan) {.sv=SV_from_str_len("Bool", 4)}
                    };

                    uint64_t out_temporal = instr.info.math_cmp_boolean_op.dest.val;
                    if (dest_t.ty != TC_TY_Primitive || dest_t.info.primitive != TC_Primitive__Bool) {
                        out_temporal = highest_temporal++;

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
                            printf("OP_OR"); break;
                        }
                        case IR_INST_TY__OP_AND: {
                            printf("OP_AND"); break;
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
                    break;
                }
                case IR_INST_TY__BRANCH:
                    break;
                case IR_INST_TY__JUMP:
                    break;
                case IR_INST_TY__RETURN_VOID:
                    break;
                case IR_INST_TY__RETURN:
                    break;
                case IR_INST_TY__GET_STRUCT_FIELD_PTR:
                    break;
                case IR_INST_TY__INDEX_PTR:
                    break;
                case IR_INST_TY__READ_PTR:
                    break;
                case IR_INST_TY__FN_CALL_RETVOID:
                    break;
                case IR_INST_TY__FN_CALL_RETVAL:
                    break;
                case IR_INST_TY__MAKE_STRUCT:
                    break;
                case IR_INST_TY__MAKE_ENUM:
                    break;
            }
        }
    }
}


/// typechecking pass (two passes) + lowering to MIR
/// performs Bidirectional Typechecking
void TC_typecheck_and_lower(
    const khash_t(sv_u64_map)* restrict identifier_map,
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
        {
            const SV fn_name = fn.fn_name.span.sv;
            printf("TYPECHECKING FN '"SV_format"'\n", SV_v_args(fn_name));
        }

        TC_Entry_LIST temporals;

        type_inference_pass_over_fn(fn, identifier_map, functions, external_functions, nct, target_info, source_info, &temporals);

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

        lowering_and_emission_pass_over_fn(fn, identifier_map, functions, external_functions, nct, target_info, source_info, &temporals, type_needing_destructor);

        // printf("-------------------\n");
        // for (size_t t = 1; t < temporals.len; t++) {
        //     const TC_Entry e = temporals.array[t];
        //     if (!e.known_temporal) {
        //         printf("  %%%zu UNKNOWN\n", t);
        //         continue;
        //     }
        //     if (e.type.ty == TC_TY_Void) {
        //         printf("ERROR: VOID TYPE %%%zu\n", t);
        //         continue;
        //     }
        //     printf("   %%%zu = {", t);
        //     print_TC_typeid(&e.type, identifier_map, nct);
        //     printf("}\n");
        // }

        // lowering_and_emission_pass_over_fn(fn, )


        // TC_typecheck_and_lower_fn(functions->array[n], identifier_map, functions, external_functions, nct, target_info, source_info, type_needing_destructor);

        TC_Entry_LIST_free(&temporals);
    }
}
