#include "typechecking.h"

#include "error.h"
#include "utils/dev.h"

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
        case TC_TY_External_Type: return a->info.external_type.name == b->info.external_type.name;
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
            syntax_error(&source_info->source, &source_info->source_filename, &type->info.external_type.raw_name, &msg);
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
                    syntax_error(&source_info->source, &source_info->source_filename, &item->raw_name, &msg);
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
                    syntax_error(&source_info->source, &source_info->source_filename, &item->raw_name, &msg);
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
                .ty = TC_TY_Void
            };
        }
        case IR_TY_Primitive: {
            return (TC_typeid){
                .ty = TC_TY_Primitive,
                .info.primitive = type->info.primitive
            };
        }
        case IR_TY_Named: {
            for(size_t n=0; n<nct->enums.len; n++) {
                if (nct->enums.array[n].name == type->info.named.name) {
                    return (TC_typeid) {
                        .ty = TC_TY_Enum,
                        .info.type_id = n
                    };
                }
            }
            for (size_t n=0; n<nct->structs.len; n++) {
                if (nct->structs.array[n].name == type->info.named.name) {
                    return (TC_typeid) {
                        .ty = TC_TY_Struct,
                        .info.type_id = n
                    };
                }
            }
            for (size_t n=0; n<nct->external_types.len; n++) {
                if (nct->external_types.array[n] == type->info.named.name) {
                    return (TC_typeid) {
                        .ty = TC_TY_External_Type,
                        .info.external_type = {
                            type->info.named.name,
                            type->info.named.raw_name
                        }
                    };
                }
            }
            const SV msg = SV_from_str("unknow type");
            syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.raw_name, &msg);
            exit(1);
        }
        case IR_TY_Ref:
        case IR_TY_Array:
        case IR_TY_Box:
        case IR_TY_Slice:
        case IR_TY_List: {
            TC_typeid* contained = malloc(sizeof(TC_typeid));
            *contained = resolve_tc_type(type->contained, nct, target_info, source_info);

            free(type->contained);

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
                .info.contained = contained
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
                if (tu.info.named.name != type->info.named.name) {
                    continue;
                }

                if (tu.contained_len != type->contained_len) {
                    char* r_msg = malloc(sizeof(char)*100);
                    snprintf(r_msg, 100, "for generic expected %lu args, but got %lu", tu.contained_len, type->contained_len);
                    const SV msg = SV_from_str(r_msg);
                    syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.raw_name, &msg);
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
                };
            }

            for (size_t n = 0; n < nct->generic_enums.len; n++) {
                if (nct->generic_enums.array[n].name == type->info.named.name) {
                    if (nct->generic_enums.array[n].generic_args.len != type->contained_len) {
                        char* r_msg = malloc(sizeof(char)*100);
                        snprintf(r_msg, 100, "for generic expected %lu args, but got %lu", nct->generic_enums.array[n].generic_args.len, type->contained_len);
                        const SV msg = SV_from_str(r_msg);
                        syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.raw_name, &msg);
                        free(r_msg);
                        exit(1);
                    }
                    materialize_kind = TC_NIRK_Enum;
                    materialize_nth = n;
                    goto materialize;

                }
            }

            for (size_t n = 0; n < nct->generic_structs.len; n++) {
                if (nct->generic_structs.array[n].name == type->info.named.name) {
                    if (nct->generic_structs.array[n].generic_args.len != type->contained_len) {
                        char* r_msg = malloc(sizeof(char)*100);
                        snprintf(r_msg, 100, "for generic expected %lu args, but got %lu", nct->generic_structs.array[n].generic_args.len, type->contained_len);
                        const SV msg = SV_from_str(r_msg);
                        syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.raw_name, &msg);
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
                syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.raw_name, &msg);
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
                                variant.raw_name,
                                new_values,
                                variant.values_len
                            });
                        }

                        const TC_enumdef new_enumdef = (TC_enumdef) {
                            ged.name,
                            ged.raw_name,
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
                                fd.raw_name,
                                ty
                            };
                        }

                        const TC_structdef new_structdef = (TC_structdef) {
                            ges.name,
                            ges.raw_name,
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
    Identifier name;
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

void TC_typecheck_and_lower_fn(
    const IR_func fn,
    const khash_t(sv_u64_map)* restrict identifier_map,
    const IR_func_LIST* restrict functions,
    const CFFI_IR_extern_fn_LIST* restrict external_functions,
    TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info
) {

    const IR_typeuse ir_ret_t = fn.fn_ret_t;
    const TC_typeid ret_t = resolve_tc_type(&ir_ret_t, nct, target_info, source_info);

    if (fn.args.len > 0) {
        UNIMPLEMENTED("function with args typechecking")
    }

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

    // typechecker state

    // todo: known value optimization??

    // (temporal - type)
    TC_typeid_LIST temporals = TC_typeid_LIST_new_with_capacity(fn.highest_temporal_number+1);

    for (size_t block_id = 0; block_id < fn.blocks.len; block_id++) {
        const IR_block block = fn.blocks.array[block_id];
        for (size_t instr_nth = 0; instr_nth < block.instructions.len; instr_nth++) {
            const IR_instr instr = block.instructions.array[instr_nth];
            switch (instr.ty) {
                case IR_INST_TY__CONST_INT: {
                    printf("CONST_INT %li -> Number temporals[%lu]\n", instr.p1.value, instr.dest.val);
                    temporals.array[instr.dest.val] = (TC_typeid) {
                        .ty = TC_TY_Primitive,
                        .info.primitive = TC_Primitive__Number,
                    };
                    break;
                }
                case IR_INST_TY__CONST_FLOAT: {
                    printf("CONST_FLOAT %f -> FloatNumber temporals[%lu]\n", instr.p1.value_float, instr.dest.val);
                    temporals.array[instr.dest.val] = (TC_typeid) {
                        .ty = TC_TY_Primitive,
                        .info.primitive = TC_Primitive__FloatNumber,
                    };
                    break;
                }
                case IR_INST_TY__MAKE_ARRAY: {
                    UNIMPLEMENTED()
                }
                case IR_INST_TY__MAKE_STRUCT: {
                    UNIMPLEMENTED()
                }
                case IR_INST_TY__VAR_DECL: {
                    printf("VAR_DECL from type '");
                    size_t dest = instr.dest.val;
                    const TC_typeid temporal_ty = temporals.array[dest];
                    print_TC_typeid(&temporal_ty, (khash_t(sv_u64_map)*)identifier_map, nct);

                    const TC_typeid declaration_ty = resolve_tc_type(instr.p2.type, nct, target_info, source_info);
                    printf("' to type '");
                    print_TC_typeid(&declaration_ty, (khash_t(sv_u64_map)*)identifier_map, nct);
                    printf("'\n");

                    if (!CMP_tc_typeid(&temporal_ty, &declaration_ty)) {
                        if (try_to_cast_to(&temporal_ty, &declaration_ty)) {
                            printf("EMIT CAST '");
                            print_TC_typeid(&temporal_ty, (khash_t(sv_u64_map)*)identifier_map, nct);
                            printf("' -> '");
                            print_TC_typeid(&declaration_ty, (khash_t(sv_u64_map)*)identifier_map, nct);
                            printf("'\n");
                        } else {
                            // TODO: proper error reporting (raw_name is not known)
                            // syntax_error(&source_info->source, &source_info->source_filename, instr.)
                            fprintf(stderr, "type collision\n");
                            exit(1);
                        }
                    }
                    break;
                }
                case IR_INST_TY__INVALID: {
                    INTERNAL_ERROR("IR_INST_TY__INVALID");
                }
                case IR_INST_TY__PHI: {
                    UNIMPLEMENTED("IR_INST_TY__PHI")
                }
                case IR_INST_TY__OP_ADD:
                case IR_INST_TY__OP_SUB:
                case IR_INST_TY__OP_DIV:
                case IR_INST_TY__OP_MUL:
                case IR_INST_TY__OP_POW: {
                    const TC_typeid v1 = temporals.array[instr.p1.src1.val];
                    const TC_typeid v2 = temporals.array[instr.p2.src2.val];
                    if (!CMP_tc_typeid(&v1, &v2)) {
                        // TODO: do not throw error immediately

                        // printf("TC v1='");
                        // print_TC_typeid(&v1, (khash_t(sv_u64_map)*)identifier_map, nct);
                        // printf("', v2='");
                        // print_TC_typeid(&v2, (khash_t(sv_u64_map)*)identifier_map, nct);
                        // printf("'\n");
                        if (try_to_cast_to(&v1, &v2)) {
                            temporals.array[instr.dest.val] = v2;
                            printf("EMIT CAST '");
                            print_TC_typeid(&v1, (khash_t(sv_u64_map)*)identifier_map, nct);
                            printf("' -> '");
                            print_TC_typeid(&v2, (khash_t(sv_u64_map)*)identifier_map, nct);
                            printf("'\n");
                        } else {
                            // TODO: proper error reporting (raw_name is not known)
                            // syntax_error(&source_info->source, &source_info->source_filename, instr.)
                            fprintf(stderr, "type collision\n");
                            exit(1);
                        }
                    } else {
                        temporals.array[instr.dest.val] = v1;
                    }

                    break;
                }
                case IR_INST_TY__OP_OR:
                    break;
                case IR_INST_TY__OP_AND:
                    break;
                case IR_INST_TY__OP_CMP_EQ:
                    break;
                case IR_INST_TY__OP_CMP_NOT_EQ:
                    break;
                case IR_INST_TY__OP_CMP_LESS:
                    break;
                case IR_INST_TY__OP_CMP_LESS_EQ:
                    break;
                case IR_INST_TY__OP_CMP_GREATER:
                    break;
                case IR_INST_TY__OP_CMP_GREATER_EQ:
                    break;
                case IR_INST_TY__STORE_PTR:
                    break;
                case IR_INST_TY__BRANCH:
                    break;
                case IR_INST_TY__JUMP:
                    break;
                case IR_INST_TY__RETURN_VOID:
                    break;
                case IR_INST_TY__RETURN:
                    break;
                case IR_INST_TY__GET_FIELD_PTR:
                    break;
                case IR_INST_TY__INDEX_PTR:
                    break;
                case IR_INST_TY__READ_PTR:
                    break;
                case IR_INST_TY__FN_CALL_VOID:
                    break;
                case IR_INST_TY__FN_CALL_RET:
                    break;
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
    const SourceInfo* restrict source_info
) {

    for (size_t n = 0; n < functions->len; n++) {
        TC_typecheck_and_lower_fn(functions->array[n], identifier_map, functions, external_functions, nct, target_info, source_info);
    }

}