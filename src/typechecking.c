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
        case TC_TY_Array:
        case TC_TY_List:
        case TC_TY_Box:
        case TC_TY_Slice: {
            return CMP_tc_typeid(a->info.contained, b->info.contained);
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
            const SV msg = SV_from_str("external type has unknow size and therefore cannot be used alone, use it with for example Ref<T>");
            syntax_error(&source_info->source, &source_info->source_filename, &type->info.external_type.span.sv, &msg);
            exit(1);
        }
        case TC_TY_Primitive: {
            return (SizeAlign) { TC_Primitive_TY_size(type->info.primitive), TC_Primitive_TY_size(type->info.primitive)};
        }
        case TC_TY_Slice:
        case TC_TY_Array: {
            return (SizeAlign) { target_info->ptr_size * 2, target_info->ptr_size }; // { ptr; len; }
        }
        case TC_TY_List: {
            return (SizeAlign) {target_info->ptr_size * 3, target_info->ptr_size }; // { ptr; len; cap; }
        }
        case TC_TY_Ref:
        case TC_TY_Box: {
            return (SizeAlign) {target_info->ptr_size, target_info->ptr_size };
        }
        case TC_TY_Struct: {
            TC_structdef* item = &nct->structs.array[type->info.type_id];

            // already calculated
            if (item->layout.size != 0) {
                return (SizeAlign) { item->layout.size, item->layout.align };
            }

            for (size_t n=0; n < in_progress_structs->len; n++) {
                if (in_progress_structs->array[n] == type->info.type_id) { // cycle detected
                    const SV msg = SV_from_str("owned cycles are forbidden, use Box<T> if you need to construct cycle in runtime");
                    syntax_error(&source_info->source, &source_info->source_filename, &item->name.span.sv, &msg);
                    exit(1);
                }
            }

            // calc
            size_t_LIST_push(in_progress_structs, type->info.type_id); // mark as in_progress
            const size_t this_in_progress_nth = in_progress_structs->len-1;

            uint32_t* field_offsets = malloc(sizeof(uint32_t)*item->fields.len);


            uint32_t offset = 0;
            uint32_t max_align = 1;

            for (uint32_t i = 0; i < item->fields.len; i++) {
                const TC_typeid* ft = &item->fields.array[i].type;

                const SizeAlign sa = path_size_align_for_materialized_type(ft, nct, in_progress_structs, in_progress_enums, target_info, source_info);

                const uint32_t fa = sa.align;
                const uint32_t fs = sa.size;

                offset = (offset + fa - 1) & ~(fa - 1); // align up
                field_offsets[i] = offset;
                offset += fs;
                if (fa > max_align) max_align = fa;
            }

            const TC_struct_layout sl = (TC_struct_layout) {
                (offset + max_align - 1) & ~(max_align - 1), // tail padding,
                max_align,
                field_offsets
            };
            item->layout = sl; // write to the struct

            // done
            in_progress_structs->array[this_in_progress_nth] = SIZE_MAX;

            return (SizeAlign){ item->layout.size, item->layout.align };
        }
        case TC_TY_Enum: {
            TC_enumdef* item = &nct->enums.array[type->info.type_id];

            if (item->size != 0) {
                return (SizeAlign){ item->size, item->align };
            }

            for (size_t n = 0; n < in_progress_enums->len; n++) {
                if (in_progress_enums->array[n] == type->info.type_id) {
                    const SV msg = SV_from_str("owned cycles are forbidden, use Box<T> if you need to construct cycle in runtime");
                    syntax_error(&source_info->source, &source_info->source_filename, &item->name.span.sv, &msg);
                    exit(1);
                }
            }

            size_t_LIST_push(in_progress_enums, type->info.type_id);
            const size_t this_in_progress_nth = in_progress_enums->len - 1;

            // discriminant is a primitive integer, align == size
            size_t disc_size;
            if      (item->variants.len <= 0xFF)       disc_size = 1;
            else if (item->variants.len <= 0xFFFF)     disc_size = 2;
            else if (item->variants.len <= 0xFFFFFFFF) disc_size = 4;
            else                                        disc_size = 8;

            size_t max_payload_size  = 0;
            size_t max_payload_align = 1;

            for (size_t v = 0; v < item->variants.len; v++) {
                const TC_enumdef_variant* variant = &item->variants.array[v];

                size_t payload_size  = 0;
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

                if (payload_size  > max_payload_size)  max_payload_size  = payload_size;
                if (variant_align > max_payload_align) max_payload_align = variant_align;
            }

            const size_t total_align   = disc_size > max_payload_align ? disc_size : max_payload_align;
            const size_t payload_start = (disc_size + max_payload_align - 1) & ~(max_payload_align - 1);
            const size_t raw_size      = payload_start + max_payload_size;
            const size_t total_size    = (raw_size + total_align - 1) & ~(total_align - 1);

            item->size  = total_size;
            item->align = total_align;

            in_progress_enums->array[this_in_progress_nth] = SIZE_MAX;

            return (SizeAlign){ total_size, total_align };
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
            for(size_t n=0; n<nct->enums.len; n++) {
                if (nct->enums.array[n].name.identifier == type->info.named.identifier) {
                    return (TC_typeid) {
                        .ty = TC_TY_Enum,
                        .info.type_id = n,
                        .full_typeuse_span = type->full_typeuse_span
                    };
                }
            }
            for (size_t n=0; n<nct->structs.len; n++) {
                if (nct->structs.array[n].name.identifier == type->info.named.identifier) {
                    return (TC_typeid) {
                        .ty = TC_TY_Struct,
                        .info.type_id = n,
                        .full_typeuse_span = type->full_typeuse_span
                    };
                }
            }
            for (size_t n=0; n<nct->external_types.len; n++) {
                if (nct->external_types.array[n].identifier == type->info.named.identifier) {
                    return (TC_typeid) {
                        .ty = TC_TY_External_Type,
                        .info.external_type = type->info.named,
                        .full_typeuse_span = type->full_typeuse_span
                    };
                }
            }
            const SV msg = SV_from_str("unknow type");
            syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.span.sv, &msg);
            exit(1);
        }
        case IR_TY_Ref:
        case IR_TY_Array:
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
                case IR_TY_Array: {
                    ty = TC_TY_Array;
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
            for (size_t n=0; n<nct->materialized_generics.len; n++) {
                const TC_materialized_generic_index_record mr = nct->materialized_generics.array[n];
                if (mr.generic.tt_ty != IR_TY_Generic) {
                    INTERNAL_ERROR("should never happen | u403");
                }

                const IR_typeuse tu = mr.generic;
                if (tu.info.named.identifier != type->info.named.identifier) {
                    continue;
                }

                if (tu.contained_len != type->contained_len) {
                    char* r_msg = malloc(sizeof(char)*100);
                    snprintf(r_msg, 100, "for generic expected %lu args, but got %lu", tu.contained_len, type->contained_len);
                    const SV msg = SV_from_str(r_msg);
                    syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.span.sv, &msg);
                    free(r_msg);
                    exit(1);
                }

                for (size_t j=0; j<tu.contained_len; j++) {
                    if (!CMP_IR_typeuse(&tu.contained[j], &type->contained[j])) {
                        materialize_kind = mr.kind;
                        materialize_nth = n;
                        goto materialize;
                    }
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
                return (TC_typeid) {
                    .ty = ty,
                    .info.type_id = mr.nth,
                    .full_typeuse_span = type->full_typeuse_span
                };
            }

            for (size_t n = 0; n < nct->generic_enums.len; n++) {
                if (nct->generic_enums.array[n].name.identifier == type->info.named.identifier) {
                    if (nct->generic_enums.array[n].generic_args.len != type->contained_len) {
                        char* r_msg = malloc(sizeof(char)*100);
                        snprintf(r_msg, 100, "for generic expected %lu args, but got %lu", nct->generic_enums.array[n].generic_args.len, type->contained_len);
                        const SV msg = SV_from_str(r_msg);
                        syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.span.sv, &msg);
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
                        char* r_msg = malloc(sizeof(char)*100);
                        snprintf(r_msg, 100, "for generic expected %lu args, but got %lu", nct->generic_structs.array[n].generic_args.len, type->contained_len);
                        const SV msg = SV_from_str(r_msg);
                        syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.span.sv, &msg);
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
                        TC_enumdef_variant_LIST new_variants = TC_enumdef_variant_LIST_new_with_capacity(ged.variants.len);
                        for (size_t j=0; j<ged.variants.len; j++) {
                            const IR_enumdef_variant variant = ged.variants.array[j];
                            TC_typeid* new_values = malloc(sizeof(TC_typeid)*variant.values_len);

                            for (size_t v=0; v<variant.values_len; v++) {
                                const IR_typeuse substituted = substitute_typeuse(&variant.values[v], &ged.generic_args, type->contained);
                                const TC_typeid ty = resolve_tc_type(&substituted, nct, target_info, source_info);
                                new_values[v] = ty;
                            }

                            TC_enumdef_variant_LIST_push(&new_variants, (TC_enumdef_variant) {
                                variant.name,
                                new_values,
                                variant.values_len
                            });
                        }

                        const TC_enumdef new_enumdef = (TC_enumdef) {
                            ged.name,
                            new_variants,
                            0,
                            0,
                            0
                        };


                        const size_t nth = nct->enums.len;
                        TC_enumdef_LIST_push(&nct->enums, new_enumdef);
                        TC_materialized_generic_index_record_LIST_push(&nct->materialized_generics, (TC_materialized_generic_index_record) {
                            *type,
                            TC_NIRK_Enum,
                            nth
                        });

                        const TC_typeid ret = (TC_typeid) {
                            .ty = TC_TY_Enum,
                            .info.type_id = nth,
                            .full_typeuse_span = type->full_typeuse_span
                        };

                        {
                            size_t_LIST in_progress_structs = size_t_LIST_new();
                            size_t_LIST in_progress_enums = size_t_LIST_new();

                            path_size_align_for_materialized_type(&ret, nct, &in_progress_structs, &in_progress_enums, target_info, source_info);

                            size_t_LIST_free(&in_progress_structs);
                            size_t_LIST_free(&in_progress_enums);
                        }

                        return ret;

                    }
                    case TC_NIRK_Struct: {
                        IR_generic_structdef ges = nct->generic_structs.array[materialize_nth];


                        TC_field_LIST new_fields = TC_field_LIST_new_with_capacity(ges.fields.len);
                        new_fields.len = ges.fields.len;

                        for (size_t j=0; j<ges.fields.len; j++) {
                            const IR_field fd = ges.fields.array[j];
                            const IR_typeuse substituted = substitute_typeuse(&fd.type, &ges.generic_args, type->contained);
                            const TC_typeid ty = resolve_tc_type(&substituted, nct, target_info, source_info);
                            new_fields.array[j] = (TC_field) {
                                fd.name,
                                ty
                            };
                        }

                        const TC_structdef new_structdef = (TC_structdef) {
                            ges.name,
                            new_fields,
                            (TC_struct_layout) {
                                0,
                                0,
                                NULL
                            }
                        };

                        const size_t nth = nct->structs.len;
                        TC_structdef_LIST_push(&nct->structs, new_structdef);
                        TC_materialized_generic_index_record_LIST_push(&nct->materialized_generics, (TC_materialized_generic_index_record) {
                            *type,
                            TC_NIRK_Struct,
                            nth
                        });


                        const TC_typeid ret = (TC_typeid) {
                            .ty = TC_TY_Struct,
                            .info.type_id = nth,
                            .full_typeuse_span = type->full_typeuse_span
                        };

                        {
                            size_t_LIST in_progress_structs = size_t_LIST_new();
                            size_t_LIST in_progress_enums = size_t_LIST_new();

                            path_size_align_for_materialized_type(&ret, nct, &in_progress_structs, &in_progress_enums, target_info, source_info);

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
    TC_InstrPos origin;
    bool is_value_known;
    union {
        void* null;
        TC_Value value;
    };
} TC_Entry;

init_prefixed_list_type(TC_Entry)



// returns new_temporal
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
                    if ((uint32_t)(out_type.info.primitive) < (uint32_t)(in_type.info.primitive)) {
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
                default: { // X
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
                    if ((uint32_t)(out_type.info.primitive) < (uint32_t)(in_type.info.primitive)) {
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
                    if ((uint32_t)(out_type.info.primitive) < (uint32_t)(in_type.info.primitive)) {
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
        char* s = malloc(sizeof(char)*100);
        snprintf(s, 100, "expected value of type "C_BOLD""SV_format""C_RESET" but found "C_BOLD""SV_format""C_RESET"", SV_v_args(in_type.full_typeuse_span.sv), SV_v_args(out_type.full_typeuse_span.sv));
        const SV msg = SV_from_str(s);
        syntax_error(&source_info->source, &source_info->source_filename, &error_info.info.math_operation.whole.sv, &msg);
        const SV hint = SV_from_str("try using "C_BOLD"cast<T>()"C_RESET" if you need to convert between primitive types");
        hint_standalone(&hint);
        exit(1);
    }

do_it:
    TC_Entry_LIST_push(temporals_map, (TC_Entry) {
        .known_temporal = true,
        .type = out_type,
        .origin = (TC_InstrPos) { .block = block_id, .instr_nth = instr_nth },
        .is_value_known = false,
        .null = NULL
    });
    const size_t new_temporal = temporals_map->len-1;

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
            RAII_drop(type->info.contained, in_obj_ptr_temporal, block_id, instr_nth, nct, temporals_map, type_needing_destructor, identifier_map);
            break;
        }

        case TC_TY_List:
        case TC_TY_Struct:
        case TC_TY_Enum:
        case TC_TY_Array: {
            RAII_emit_typed_destructor(type, in_obj_ptr_temporal, block_id, instr_nth, nct, temporals_map, type_needing_destructor, identifier_map);
            break;
        }

    }
}


void lowering_and_emission_pass_over_fn(
    const IR_func fn,
    const khash_t(sv_u64_map)* restrict identifier_map,
    const IR_func_LIST* restrict functions,
    const CFFI_IR_extern_fn_LIST* restrict external_functions,
    TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info,
    const TC_Entry_LIST* restrict temporals_map
) {
    UNIMPLEMENTED()
}



void type_inference_pass_over_fn(
    const IR_func fn,
    const khash_t(sv_u64_map)* restrict identifier_map,
    const IR_func_LIST* restrict functions,
    const CFFI_IR_extern_fn_LIST* restrict external_functions,
    TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info,
    TC_Entry_LIST* restrict temporals_map,
    TC_typeid_LIST* restrict type_needing_destructor
) {
    const IR_instr_error_info_LIST* error_info_list = &fn.error_info_list;

    const IR_typeuse ir_ret_t = fn.fn_ret_t;
    const TC_typeid ret_t = resolve_tc_type(&ir_ret_t, nct, target_info, source_info);


    TC_arg_LIST args = TC_arg_LIST_new_with_capacity(fn.args.len);
    args.len = fn.args.len;
    for (size_t n=0; n<fn.args.len; n++) {
        const TC_typeid type = resolve_tc_type(&fn.args.array[n].type, nct, target_info, source_info);
        args.array[n] = (TC_arg) {
            type,
            fn.args.array[n].name
        };
    }

    // now we have args and return with real types, we now can check against that

    TC_InstrPos_LIST visited_phis = TC_InstrPos_LIST_new();


    /// `is_value_known` is always false in this pass
    *temporals_map = TC_Entry_LIST_new_with_capacity(fn.highest_temporal_number+1);

    TC_Entry_LIST_push(temporals_map, (TC_Entry) { .known_temporal = false });
    for (size_t n = 0; n < args.len; n++) {
        // printf("ARG %%%lu {", n+1);
        // print_TC_typeid(&args.array[n].type, identifier_map, nct);
        // printf("}\n");
        TC_Entry_LIST_push(temporals_map, (TC_Entry) {
            .known_temporal = true,
            .type = args.array[n].type,
            .origin = (TC_InstrPos) {0},
            .is_value_known = false,
            .null = NULL
        });
    }

    while (temporals_map->len < fn.highest_temporal_number+1) {
        TC_Entry_LIST_push(temporals_map, (TC_Entry) { .known_temporal = false });
    }


    // reverse instr walk + jump back on PHI to type-resolve

    for (ptrdiff_t block_id = (ptrdiff_t)fn.blocks.len - 1; block_id >= 0; block_id--) {
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

            switch (instr.ty) {
                case IR_INST_TY__DROP: {
                    const TC_typeid type = resolve_tc_type(instr.info.drop.type, nct, target_info, source_info);
                    temporals_map->array[instr.info.drop.dest.val] = (TC_Entry) {
                        .known_temporal = true,
                        .type = type,
                        .origin = (TC_InstrPos) { .block = block_id, .instr_nth = instr_nth },
                        .is_value_known = false,
                        .null = NULL,
                    };
                    break;
                }
                case IR_INST_TY__VAR_DECL: {
                    const TC_typeid type = resolve_tc_type(instr.info.var_decl.type, nct, target_info, source_info);
                    if (temporals_map->array[instr.info.var_decl.dest.val].known_temporal) {
                        if (!CMP_tc_typeid(&temporals_map->array[instr.info.var_decl.dest.val].type, &type)) {
                            // type error
                            fprintf(stderr, "var_decl type collision | make proper error later\n");
                            exit(1);
                        }
                    } else {
                        // %dest is dead value (never used)
                        temporals_map->array[instr.info.var_decl.dest.val] = (TC_Entry) {
                            .known_temporal = true,
                            .type = type,
                            .origin = (TC_InstrPos) { .block = block_id, .instr_nth = instr_nth },
                            .is_value_known = false,
                            .null = NULL,
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
                    if (!dest.known_temporal) {
                        // INTERNAL_ERROR()
                        printf("  skipped MATH_OP, waiting for second pass\n");
                        break;
                    }

                    if (temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].known_temporal) {
                        // ok (is checked in next pass)
                    } else {
                        temporals_map->array[instr.info.math_cmp_boolean_op.src1.val] = (TC_Entry) {
                            .known_temporal = true,
                            .type = dest.type,
                            .origin = (TC_InstrPos) { .block = block_id, .instr_nth = instr_nth },
                            .is_value_known = false,
                            .null = NULL,
                        };
                    }

                    if (temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].known_temporal) {
                        // ok (is checked in next pass)
                    } else {
                        temporals_map->array[instr.info.math_cmp_boolean_op.src2.val] = (TC_Entry) {
                            .known_temporal = true,
                            .type = dest.type,
                            .origin = (TC_InstrPos) { .block = block_id, .instr_nth = instr_nth },
                            .is_value_known = false,
                            .null = NULL,
                        };
                    }

                    break;
                }
                case IR_INST_TY__CONST_INT: {
                    const TC_Entry dest = temporals_map->array[instr.info.const_int.dest.val];
                    if (!dest.known_temporal) {
                        printf("skipping [%%%lu = CONST_INT]\n", instr.info.const_int.dest.val);
                    }
                    break;
                }
                case IR_INST_TY__RETURN: {
                    const size_t ret_temporal = instr.info.return_value.val;
                    if (temporals_map->array[ret_temporal].known_temporal) {
                        INTERNAL_ERROR()

                    }
                    temporals_map->array[ret_temporal] = (TC_Entry) {
                        .known_temporal = true,
                        .type = ret_t,
                        .origin = (TC_InstrPos) { .block = block_id, .instr_nth = instr_nth },
                        .is_value_known = false,
                        .null = NULL,
                    };

                    break;
                }
                case IR_INST_TY__OP_CMP_EQ:
                case IR_INST_TY__OP_CMP_NOT_EQ:
                case IR_INST_TY__OP_CMP_LESS:
                case IR_INST_TY__OP_CMP_LESS_EQ:
                case IR_INST_TY__OP_CMP_GREATER:
                case IR_INST_TY__OP_CMP_GREATER_EQ: {
                    const TC_Entry dest = temporals_map->array[instr.info.math_cmp_boolean_op.dest.val];
                    if (!dest.known_temporal) {
                        temporals_map->array[instr.info.math_cmp_boolean_op.dest.val] = (TC_Entry) {
                            .known_temporal = true,
                            .type = (TC_typeid) {
                                .ty = TC_TY_Primitive,
                                .info.primitive = TC_Primitive__Bool,
                                .full_typeuse_span = (TokenSpan){SV_from_str("Bool")}
                            },
                            .origin = (TC_InstrPos) { .block = block_id, .instr_nth = instr_nth },
                            .is_value_known = false,
                            .null = NULL,
                        };
                    }
                    // if one of args is known and other unknown, infer
                    if (temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].known_temporal && !temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].known_temporal) {
                        temporals_map->array[instr.info.math_cmp_boolean_op.src2.val] = (TC_Entry) {
                            .known_temporal = true,
                            .type = temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].type,
                            .origin = (TC_InstrPos) { .block = block_id, .instr_nth = instr_nth },
                            .is_value_known = false,
                            .null = NULL,
                        };
                    } else if (temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].known_temporal && !temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].known_temporal) {
                        temporals_map->array[instr.info.math_cmp_boolean_op.src1.val] = (TC_Entry) {
                            .known_temporal = true,
                            .type = temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].type,
                            .origin = (TC_InstrPos) { .block = block_id, .instr_nth = instr_nth },
                            .is_value_known = false,
                            .null = NULL,
                        };
                    }
                    break;
                }
                case IR_INST_TY__BRANCH: {
                    const TC_Entry dest = temporals_map->array[instr.info.branch.dest.val];
                    if (dest.known_temporal) {
                        // compare types in the next pass
                        break;
                    }
                    temporals_map->array[instr.info.branch.dest.val] = (TC_Entry) {
                        .known_temporal = true,
                        .type = (TC_typeid) {
                            .ty = TC_TY_Primitive,
                            .info.primitive = TC_Primitive__Bool,
                            .full_typeuse_span = (TokenSpan){SV_from_str("Bool")}
                        },
                        .origin = (TC_InstrPos) { .block = block_id, .instr_nth = instr_nth },
                        .is_value_known = false,
                        .null = NULL,
                    };
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
                        printf("  PHI skipped, unknown dest, probably filled later by PHI\n");
                        break;
                    }

                    ptrdiff_t highest_block = block_id;
                    for (size_t n = 0; n<phi_args->len; n++) {
                        const IR_temporal_block_phi_record arg = phi_args->array[n];
                        if (!temporals_map->array[arg.temp.val].known_temporal) {
                            temporals_map->array[arg.temp.val] = (TC_Entry) {
                                .known_temporal = true,
                                .type = dest.type,
                                .origin = (TC_InstrPos) { .block = block_id, .instr_nth = instr_nth },
                                .is_value_known = false,
                                .null = NULL,
                            };
                        }
                        if ((ptrdiff_t)(arg.block) > highest_block) {
                            highest_block = (ptrdiff_t)arg.block;
                        }
                    }

                    TC_InstrPos_LIST_push(&visited_phis, (TC_InstrPos) { .block = block_id, .instr_nth = instr_nth });

                    if (highest_block > block_id) {
                        printf("block jumping to Block<%ld> (from %ld)\n", highest_block, block_id);
                        block_id = highest_block + 1; // because of (block_id--) in the for loop
                        perf_jump = true;
                    }

                    break;
                }
                case IR_INST_TY__EXPLICIT_CAST: {
                    const TC_typeid dest_type = temporals_map->array[instr.info.explicit_cast.dest.val].type;
                    const IR_typeuse* raw_cast_type = instr.info.explicit_cast.type;
                    const TC_typeid cast_type = resolve_tc_type(raw_cast_type, nct, target_info, source_info);

                    if (!CMP_tc_typeid(&dest_type, &cast_type)) {
                        printf("ERROR info: ");
                        printf("dest_type = {");
                        print_TC_typeid(&dest_type, identifier_map, nct);
                        printf("}, cast_type = {");
                        print_TC_typeid(&cast_type, identifier_map, nct);
                        printf("}\n");
                        fprintf(stderr, "invalid type cast, expected different type as output of casting\n");
                        exit(1);
                    }

                    if (!temporals_map->array[instr.info.explicit_cast.src.val].known_temporal) {
                        INTERNAL_ERROR("unknown cast intake type");
                    }

                    const TC_typeid cast_intake_type = temporals_map->array[instr.info.explicit_cast.src.val].type;

                    // todo: add verification cast can happen (you cannot cast anything to anything, only numbers around)

                    break;
                }
                case IR_INST_TY__MOVE: {
                    const TC_Entry dest = temporals_map->array[instr.info.move.dest.val];
                    if (!dest.known_temporal) {
                        printf("  MOVE skipped, unknown dest, probably filled later by PHI\n");
                        break;
                    }
                    if (temporals_map->array[instr.info.move.src.val].known_temporal) {
                        // ok (is checked in next pass)
                    } else {
                        temporals_map->array[instr.info.move.src.val] = (TC_Entry) {
                            .known_temporal = true,
                            .type = dest.type,
                            .origin = (TC_InstrPos) { .block = block_id, .instr_nth = instr_nth },
                            .is_value_known = false,
                            .null = NULL,
                        };
                    }

                    break;
                }
                default: {
                    break;
                }
            }
        }
    }


}

/// typechecking pass + lowering to MIR
void TC_typecheck_and_lower(
    const khash_t(sv_u64_map)* restrict identifier_map,
    const IR_func_LIST* restrict functions,
    const CFFI_IR_extern_fn_LIST* restrict external_functions,
    TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info,
    TC_typeid_LIST* restrict type_needing_destructor
) {


    for (size_t n = 0; n < functions->len; n++) {
        const IR_func fn = functions->array[n];
        {
            const SV fn_name = fn.fn_name.span.sv;
            printf("TYPECHECKING FN '"SV_format"'\n", SV_v_args(fn_name));
        }

        TC_Entry_LIST temporals;

        type_inference_pass_over_fn(fn, identifier_map, functions, external_functions, nct, target_info, source_info, &temporals, type_needing_destructor);

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
            printf("   %%%lu = {"SV_format"}\n", t, SV_v_args(e.type.full_typeuse_span.sv));
        }

        // lowering_and_emission_pass_over_fn(fn, )




        // TC_typecheck_and_lower_fn(functions->array[n], identifier_map, functions, external_functions, nct, target_info, source_info, type_needing_destructor);

        TC_Entry_LIST_free(&temporals);
    }

}