#include "lowering.h"

#include "materialization.h"
#include "perf.h"
#include "passes/type_utils.h"
#include "passes/typeinference/typeinference.h"
#include "typeresolve/type_resolve.h"
#include "error.h"
#include "utils/color.h"
#include "passes/parser/parser.h"
#include <ctype.h>

#include "overloading/resolve.h"

bool is_type_on_stack(const TC_typeid* t, const TC_TypeTable* nct, const TargetInfo* target_info) {
    const SizeAlign sa = calc_size_of(t, nct, target_info);
    return sa.size > target_info->reg_size ||
        t->ty == TC_TY_Array ||
        t->ty == TC_TY_Struct ||
        t->ty == TC_TY_Enum ||
        t->ty == TC_TY_Slice ||
        t->ty == TC_TY_List;
}


IR_temporal resolve(IR_temporal in, const IR_rewrite_LIST* restrict rewrite_map) {
    IR_temporal tmp = in;
    uint32_t cnt = 0;
    while(rewrite_map->array[tmp.val].has_rewrite) {
        if(cnt > 1000) {
            INTERNAL_ERROR("infinite rewrite (.in=%%%lu, .tmp=%%%lu)", in.val, tmp.val);
        }
        tmp = rewrite_map->array[tmp.val].rewrite;
        cnt++;
        if(tmp.val == in.val) {
            INTERNAL_ERROR("cyclic rewrite (.in=%%%lu)", in.val);
        }
    }
    // if(in.val != tmp.val) printf("resolved %%%lu as %%%lu\n", in.val, tmp.val);
    return tmp;
}

void resolve_patterns(Pattern_LIST* restrict patterns, const IR_rewrite_LIST* restrict rewrite_map) {
    for(size_t n = 0; n < patterns->len; n++) {
        switch (patterns->array[n].kind) {
            case PAT_LITERAL_INT:
            case PAT_LITERAL_FLOAT:
                break;
            case PAT_BINDING: {
                patterns->array[n].info.binding.temp = resolve(patterns->array[n].info.binding.temp, rewrite_map);
                break;
            }
            case PAT_VARIANT: {
                resolve_patterns(&patterns->array[n].info.variant.sub_patterns, rewrite_map);
                break;
            }
            case PAT_WILDCARD:
                break;
        }
    }
}





IR_instr instr_rewrite_process(const IR_instr instr, const IR_rewrite_LIST* restrict rewrite_map) {
    IR_instr out = instr;
    switch (out.ty) {
        case IR_INST_TY__INVALID:
            break;
        case IR_INST_TY__PHI: {
            out.info.phi.dest = resolve(out.info.phi.dest, rewrite_map);
            for(size_t n = 0; n < out.info.phi.args->len; n++) {
                out.info.phi.args->array[n].temp = resolve(out.info.phi.args->array[n].temp, rewrite_map);
            }
            break;
        }
        case IR_INST_TY__DROP: {
            out.info.drop.dest = resolve(instr.info.drop.dest, rewrite_map);
            break;
        }
        case IR_INST_TY__MOVE: {
            out.info.move.src = resolve(out.info.move.src, rewrite_map);
            out.info.move.dest = resolve(out.info.move.dest, rewrite_map);
            break;
        }
        case IR_INST_TY__ASSIGN: {
            out.info.assign.dest = resolve(out.info.assign.dest, rewrite_map);
            out.info.assign.new = resolve(out.info.assign.new, rewrite_map);
            out.info.assign.old = resolve(out.info.assign.old, rewrite_map);
            break;
        }
        case IR_INST_TY__REF_OF: {
            out.info.ref_of.dest = resolve(out.info.ref_of.dest, rewrite_map);
            out.info.ref_of.src = resolve(out.info.ref_of.src, rewrite_map);
            break;
        }
        case IR_INST_TY__VAR_DECL: {
            out.info.var_decl.dest = resolve(out.info.var_decl.dest, rewrite_map);
            break;
        }
        case IR_INST_TY__STORE_PTR: {
            out.info.store_ptr.ptr = resolve(out.info.store_ptr.ptr, rewrite_map);
            out.info.store_ptr.value = resolve(out.info.store_ptr.value, rewrite_map);
            break;
        }
        case IR_INST_TY__EXPLICIT_CAST: {
            out.info.explicit_cast.dest = resolve(out.info.explicit_cast.dest, rewrite_map);
            out.info.explicit_cast.src = resolve(out.info.explicit_cast.src, rewrite_map);
            break;
        }
        case IR_INST_TY__CONST_INT: {
            out.info.const_int.dest = resolve(out.info.const_int.dest, rewrite_map);
            break;
        }
        case IR_INST_TY__CONST_FLOAT: {
            out.info.const_float.dest = resolve(out.info.const_float.dest, rewrite_map);
            break;
        }
        case IR_INST_TY__CONST_STRING: {
            out.info.const_string.dest = resolve(out.info.const_string.dest, rewrite_map);
            break;
        }
        case IR_INST_TY__MAKE_ARRAY: {
            out.info.make_array.dest = resolve(out.info.make_array.dest, rewrite_map);
            for(size_t n = 0; n < out.info.make_array.items->len; n++) {
                out.info.make_array.items->array[n] = resolve(out.info.make_array.items->array[n], rewrite_map);
            }
            break;
        }
        case IR_INST_TY__OP_ADD:
        case IR_INST_TY__OP_SUB:
        case IR_INST_TY__OP_DIV:
        case IR_INST_TY__OP_MUL:
        case IR_INST_TY__OP_POW:
        // case IR_INST_TY__OP_OR:
        // case IR_INST_TY__OP_AND:
        case IR_INST_TY__OP_CMP_EQ:
        case IR_INST_TY__OP_CMP_NOT_EQ:
        case IR_INST_TY__OP_CMP_LESS:
        case IR_INST_TY__OP_CMP_LESS_EQ:
        case IR_INST_TY__OP_CMP_GREATER:
        case IR_INST_TY__OP_CMP_GREATER_EQ: {
            out.info.math_cmp_boolean_op.dest = resolve(out.info.math_cmp_boolean_op.dest, rewrite_map);
            out.info.math_cmp_boolean_op.src1 = resolve(out.info.math_cmp_boolean_op.src1, rewrite_map);
            out.info.math_cmp_boolean_op.src2 = resolve(out.info.math_cmp_boolean_op.src2, rewrite_map);
            break;
        }
        case IR_INST_TY__BRANCH: {
            out.info.branch.cond = resolve(out.info.branch.cond, rewrite_map);
            break;
        }
        case IR_INST_TY__TO_BOOL: {
            out.info.to_bool.src = resolve(out.info.to_bool.src, rewrite_map);
            out.info.to_bool.dest = resolve(out.info.to_bool.dest, rewrite_map); // ?
        }
        case IR_INST_TY__JUMP:
            break;
        case IR_INST_TY__RETURN_VOID:
            break;
        case IR_INST_TY__RETURN: {
            out.info.return_value = resolve(out.info.return_value, rewrite_map);
            break;
        }
        case IR_INST_TY__GET_STRUCT_FIELD_PTR: {
            out.info.get_struct_field_ptr.struct_ptr = resolve(out.info.get_struct_field_ptr.struct_ptr, rewrite_map);
            out.info.get_struct_field_ptr.dest = resolve(out.info.get_struct_field_ptr.dest, rewrite_map);
            break;
        }
        case IR_INST_TY__INDEX_PTR: {
            out.info.index_ptr.index_val = resolve(out.info.index_ptr.index_val, rewrite_map);
            out.info.index_ptr.array_ptr = resolve(out.info.index_ptr.array_ptr, rewrite_map);
            out.info.index_ptr.dest = resolve(out.info.index_ptr.dest, rewrite_map);
            break;
        }
        case IR_INST_TY__READ_PTR: {
            out.info.read_ptr.dest = resolve(out.info.read_ptr.dest, rewrite_map);
            out.info.read_ptr.ptr = resolve(out.info.read_ptr.ptr, rewrite_map);
            break;
        }
        case IR_INST_TY__FN_CALL_RETVOID: {
            for(size_t n = 0; n < out.info.fn_call_retvoid.args->len; n++) {
                out.info.fn_call_retvoid.args->array[n] = resolve(out.info.fn_call_retvoid.args->array[n], rewrite_map);
            }
            break;
        }
        case IR_INST_TY__FN_CALL_RETVAL: {
            out.info.fn_call_retval.dest = resolve(out.info.fn_call_retval.dest, rewrite_map);
            for(size_t n = 0; n < out.info.fn_call_retval.args->len; n++) {
                out.info.fn_call_retval.args->array[n] = resolve(out.info.fn_call_retval.args->array[n], rewrite_map);
            }
            break;
        }
        case IR_INST_TY__MAKE_STRUCT: {
            out.info.make_struct.dest = resolve(out.info.make_struct.dest, rewrite_map);
            for(size_t n = 0; n < out.info.make_struct.args->len; n++) {
                out.info.make_struct.args->array[n].value = resolve(out.info.make_struct.args->array[n].value, rewrite_map);
            }
            break;
        }
        case IR_INST_TY__MAKE_ENUM: {
            out.info.make_enum.dest = resolve(out.info.make_enum.dest, rewrite_map);
            for(size_t n = 0; n < out.info.make_enum.args->len; n++) {
                out.info.make_enum.args->array[n] = resolve(out.info.make_enum.args->array[n], rewrite_map);
            }
            break;
        }
        case IR_INST_TY__MATCH_META__TARGET: {
            out.info.match_meta__target.target = resolve(out.info.match_meta__target.target, rewrite_map);
            break;
        }
        case IR_INST_TY__MATCH_META__MATCH: {
            out.info.match_meta__match.target = resolve(out.info.match_meta__match.target, rewrite_map);
            resolve_patterns(&out.info.match_meta__match.patterns, rewrite_map);
            break;
        }
        case ENUM_GET_FIELD_PTR: {
            out.info.get_struct_field_ptr.struct_ptr = resolve(out.info.get_struct_field_ptr.struct_ptr, rewrite_map);
            out.info.get_struct_field_ptr.dest = resolve(out.info.get_struct_field_ptr.dest, rewrite_map);
            break;
        }
        case IR_INST_TY__ENUM_VARIANT_NUMBER_EXTRACT: {
            out.info.enum_variant_number_extract.enum_src = resolve(out.info.enum_variant_number_extract.enum_src, rewrite_map);
            out.info.enum_variant_number_extract.dest = resolve(out.info.enum_variant_number_extract.dest, rewrite_map);
            break;
        }
        case IR_INST_TY__ENUM_VARIANT_NUMBER_FROM_TYPE: {
            out.info.enum_variant_number_from_type.dest = resolve(out.info.enum_variant_number_from_type.dest, rewrite_map);
            break;
        }
        case IR_INST_TY__MAKE_BOX: {
            out.info.make_box.dest = resolve(out.info.make_box.dest, rewrite_map);
            break;
        }
    }
    return out;
}




MIR_func lowering_and_emission_pass_over_fn(
    const IR_func fn,
    const khash_t(sv_u64_map)* restrict identifier_map,
    const IR_func_LIST* restrict functions,
    // const CFFI_IR_extern_fn_LIST* restrict external_functions,
    TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info,
    TC_Entry_LIST* restrict temporals_map,
    const IR_temporal_LIST promote_to_stack_temps,
    TC_typeid_LIST* restrict type_needing_destructor
) {
    printf("LINEAR TYPECHECK AND LOWERING:\n");


    IR_rewrite_LIST rewrite_map = IR_rewrite_LIST_new_with_capacity(temporals_map->len);
    for(size_t n = 0; n < temporals_map->len; n++) {
        IR_rewrite_LIST_push(&rewrite_map, (IR_rewrite) {
            false,
            (IR_temporal) {0}
        });
    }

    TC_Value_LIST temporals_map_values = TC_Value_LIST_new_with_capacity(fn.highest_temporal_number + 1);
    size_t highest_temporal = fn.highest_temporal_number + 1;

    MIR_block_LIST mir_blocks = MIR_block_LIST_new_with_capacity(fn.blocks.len);
    MIR_temporal_LIST mir_temporals = MIR_temporal_LIST_new_with_capacity(highest_temporal);

    for (ptrdiff_t block_id = 0; block_id < (ptrdiff_t) fn.blocks.len; block_id++) {
        const IR_block block = fn.blocks.array[block_id];
        MIR_instr_LIST mir_instrs = MIR_instr_LIST_new_with_capacity(block.instructions.len);

        // printf("  BLOCK %lu:\n", block_id);
        for (size_t instr_nth = 0; instr_nth < block.instructions.len; instr_nth++) {
            const IR_instr instr_original = block.instructions.array[instr_nth];

            const IR_instr instr = instr_rewrite_process(instr_original, &rewrite_map);


            switch (instr.ty) {
                case IR_INST_TY__INVALID: {
                    INTERNAL_ERROR("IR_INST_TY__INVALID");
                    break;
                }

                case IR_INST_TY__ASSIGN: {
                    const TC_typeid old_ty = temporals_map->array[instr.info.assign.old.val].type;
                    const TC_typeid value_ty = TC_typeid_deep_copy(&temporals_map->array[instr.info.assign.new.val].type);
                    const SV expr_sv = fn.error_info_list.array[instr.error_info_index].kind == IR_IEI_store_ptr
                                           ? fn.error_info_list.array[instr.error_info_index].info.store_ptr.expr.sv
                                           : (SV){0};
                    if (old_ty.ty == TC_TY_Ref) {
                        const TC_typeid expected_ty = TC_typeid_deep_copy(old_ty.info.contained);
                        const IR_temporal coerced_val = perf_autos(
                            &instr.info.assign.new,
                            &value_ty,
                            &expected_ty,
                            &highest_temporal,
                            identifier_map,
                            nct,
                            temporals_map,
                            source_info,
                            &expr_sv,
                            &temporals_map_values,
                            &promote_to_stack_temps,
                            target_info,
                            &mir_instrs
                        );
                        mir_emit(&mir_instrs, (MIR_instr) {
                            .type = MIR_TY__DROP,
                            .info.drop = {
                                .dest = instr.info.assign.old.val,
                                .type = TC_typeid_deep_copy(&old_ty),
                                .drop_inside = true
                            }
                        });
                        mir_emit(&mir_instrs, (MIR_instr) {
                            .type = MIR_TY__STORE,
                            .info.store = {
                                .ptr = instr.info.assign.old.val,
                                .val = coerced_val.val,
                                .is_val_stack =is_type_on_stack(&expected_ty, nct, target_info),  //coerced_val.val != instr.info.assign.new.val,
                                .val_type = TC_typeid_deep_copy(&expected_ty)
                            }
                        });
                        rewrite_map.array[instr.info.assign.dest.val].has_rewrite = true;
                        rewrite_map.array[instr.info.assign.dest.val].rewrite = instr.info.assign.old;
                        break;
                    }
                    const TC_typeid expected_ty = TC_typeid_deep_copy(&old_ty);
                    const IR_temporal coerced_val = perf_autos(
                        &instr.info.assign.new,
                        &value_ty,
                        &expected_ty,
                        &highest_temporal,
                        identifier_map,
                        nct,
                        temporals_map,
                        source_info,
                        &expr_sv,
                        &temporals_map_values,
                        &promote_to_stack_temps,
                        target_info,
                        &mir_instrs
                    );
                    bool is_value = true;
                    for (size_t n = 0; n < promote_to_stack_temps.len; n++) {
                        if (
                            promote_to_stack_temps.array[n].val == instr.info.assign.old.val ||
                            promote_to_stack_temps.array[n].val == instr.info.assign.dest.val
                        ) {
                            is_value = false;
                            break;
                        }
                    }
                    if (is_value) {
                        mir_emit(&mir_instrs, (MIR_instr) {
                            .type = MIR_TY__MOVE,
                            .info.move = {
                                .dest = instr.info.assign.dest.val,
                                .src = coerced_val.val,
                                .type = TC_typeid_deep_copy(&expected_ty)
                            }
                        });
                        mir_emit(&mir_instrs, (MIR_instr) {
                            .type = MIR_TY__DROP,
                            .info.drop = {
                                .dest = instr.info.assign.old.val,
                                .type = TC_typeid_deep_copy(&expected_ty),
                                .drop_inside = true
                            }
                        });
                    } else {
                        mir_emit(&mir_instrs, (MIR_instr) {
                            .type = MIR_TY__DROP,
                            .info.drop = {
                                .dest = instr.info.assign.old.val,
                                .type = TC_typeid_deep_copy(&expected_ty),
                                .drop_inside = true
                            }
                        });
                        mir_emit(&mir_instrs, (MIR_instr) {
                            .type = MIR_TY__STORE,
                            .info.store = {
                                .ptr = instr.info.assign.old.val,
                                .val = coerced_val.val,
                                .is_val_stack = true,
                                .val_type = TC_typeid_deep_copy(&expected_ty)
                            }
                        });
                        rewrite_map.array[instr.info.assign.dest.val].has_rewrite = true;
                        rewrite_map.array[instr.info.assign.dest.val].rewrite = instr.info.assign.old;
                    }
                    break;
                }

                // case IR_INST_TY__ASSIGN: {
                //     bool is_value = true;
                //     // if(
                //     //     temporals_map->array[instr.info.assign.old.val].type.ty == TC_TY_Ref &&
                //     //     temporals_map->array[instr.info.assign.new.val].type.ty != TC_TY_Ref
                //     // ) {
                //     //     // should be DROP of the Ref<T>, drop T
                //     //     mir_emit(&mir_instrs, (MIR_instr) {
                //     //         .type = MIR_TY__DROP,
                //     //         .info.drop = {
                //     //             .dest = instr.info.assign.old.val,
                //     //             .type = TC_typeid_deep_copy(&temporals_map->array[instr.info.assign.old.val].type),
                //     //             .drop_inside = true
                //     //         }
                //     //     });
                //     //     mir_emit(&mir_instrs, (MIR_instr) {
                //     //         .type = MIR_TY__STORE,
                //     //         .info.store = {
                //     //             .ptr = instr.info.assign.old.val,
                //     //             .val = instr.info.assign.new.val,
                //     //             .is_val_stack = true,
                //     //             .val_type =
                //     //         }
                //     //     });
                //     //     // printf("   DROP %%%lu (ASSIGN) {drop value in}\n", instr.info.assign.old.val);
                //     //     // printf("   STORE INTO %%%lu VALUE %%%lu (ASSIGN)\n", instr.info.assign.old.val, instr.info.assign.new.val);
                //     //     rewrite_map.array[instr.info.assign.dest.val].has_rewrite = true;
                //     //     rewrite_map.array[instr.info.assign.dest.val].rewrite = instr.info.assign.old;
                //     //     break;
                //     // }
                //     for(size_t n = 0; n < promote_to_stack_temps.len; n++) {
                //         if(
                //             promote_to_stack_temps.array[n].val == instr.info.assign.old.val ||
                //             promote_to_stack_temps.array[n].val == instr.info.assign.dest.val
                //         ) {
                //             is_value = false;
                //             break;
                //         }
                //     }
                //
                //     if (is_value) {
                //         mir_emit(&mir_instrs, (MIR_instr) {
                //             .type = MIR_TY__MOVE,
                //             .info.move = {
                //                 .dest = instr.info.assign.dest.val,
                //                 .src = instr.info.assign.new.val,
                //                 .type = TC_typeid_deep_copy(&temporals_map->array[instr.info.assign.old.val].type)
                //             }
                //         });
                //         mir_emit(&mir_instrs, (MIR_instr) {
                //             .type = MIR_TY__DROP,
                //             .info.drop = {
                //                 .dest = instr.info.assign.old.val,
                //                 .type = TC_typeid_deep_copy(&temporals_map->array[instr.info.assign.old.val].type),
                //                 .drop_inside = true
                //             }
                //         });
                //         // printf("   %%%lu = MOVE %%%lu (ASSIGN)\n", instr.info.assign.dest.val, instr.info.assign.new.val);
                //         // printf("   DROP %%%lu (ASSIGN)\n", instr.info.assign.old.val);
                //     } else {
                //         mir_emit(&mir_instrs, (MIR_instr) {
                //             .type = MIR_TY__DROP,
                //             .info.drop = {
                //                 .dest = instr.info.assign.old.val,
                //                 .type = TC_typeid_deep_copy(&temporals_map->array[instr.info.assign.old.val].type),
                //                 .drop_inside = true
                //             }
                //         });
                //         mir_emit(&mir_instrs, (MIR_instr) {
                //             .type = MIR_TY__STORE,
                //             .info.store = {
                //                 .ptr = instr.info.assign.old.val,
                //                 .val = instr.info.assign.new.val,
                //                 .is_val_stack = true,
                //                 .val_type = TC_typeid_deep_copy(&temporals_map->array[instr.info.assign.new.val].type)
                //             }
                //         });
                //         // printf("   DROP %%%lu (ASSIGN) {drop value in}\n", instr.info.assign.old.val);
                //         // printf("   STORE INTO %%%lu VALUE %%%lu (ASSIGN)\n", instr.info.assign.old.val, instr.info.assign.new.val);
                //
                //         rewrite_map.array[instr.info.assign.dest.val].has_rewrite = true;
                //         rewrite_map.array[instr.info.assign.dest.val].rewrite = instr.info.assign.old;
                //     }
                //     break;
                // }

                case IR_INST_TY__PHI: {
                    // Typecheck: Ensure all incoming PHI branches are the exact same type
                    // instr.info.phi.dest.val is the source of truth
                    const TC_typeid dest_t = temporals_map->array[instr.info.phi.dest.val].type;

                    for (size_t n = 0; n < instr.info.phi.args->len; n++) {
                        const IR_temporal_block_phi_record* record = &instr.info.phi.args->array[n];

                        // if (dest_t.ty == TC_TY_Primitive && dest_t.info.primitive == TC_Primitive__Bool) {
                        //     if (!is_type_eq(&dest_t, &temporals_map->array[record->temp.val].type)) {
                        //         // perf_bool()
                        //         IR_temporal coerced = perf_bool(
                        //             &record->temp,
                        //             &temporals_map->array[record->temp.val].type,
                        //             &highest_temporal,
                        //             temporals_map,
                        //             &promote_to_stack_temps,
                        //             nct,
                        //             target_info
                        //         );
                        //         rewrite_map.array[record->temp.val].has_rewrite = true;
                        //         rewrite_map.array[record->temp.val].rewrite = coerced;
                        //     }
                        //     continue;
                        // }
                        if (!is_type_eq(&dest_t, &temporals_map->array[record->temp.val].type)) {
                            const SV dest_t_sv = format_TC_typeid(&dest_t, identifier_map, nct);
                            const SV sec_t_sv = format_TC_typeid(&temporals_map->array[record->temp.val].type, identifier_map, nct);
                            INTERNAL_ERROR("PHI: should not happen because of backward inference pass, .dest_t = '"SV_format"', .sec_t = '"SV_format"'", SV_v_args(dest_t_sv), SV_v_args(sec_t_sv));
                        }
                    }

                    bool promoted = false;
                    for(size_t n = 0; n < promote_to_stack_temps.len; n++) {
                        if(promote_to_stack_temps.array[n].val == instr.info.phi.dest.val) {
                            promoted = true;
                            break;
                        }
                    }
                    if(promoted) {
                        // for(size_t n = 0; n < instr.info.phi.args->len; n++) {
                        //     rewrite_map.array[instr.info.phi.args->array[n].temp.val].has_rewrite = true;
                        //     rewrite_map.array[instr.info.phi.args->array[n].temp.val].rewrite = instr.info.phi.dest;
                        // }
                        // break;
                        IR_temporal slot = {0};
                        bool have_slot = false;

                        for (size_t n = 0; n < instr.info.phi.args->len; n++) {
                            IR_temporal a = instr.info.phi.args->array[n].temp;
                            if (a.val == instr.info.phi.dest.val) continue;
                            if (!temporals_map->array[a.val].is_addressable) continue;
                            slot = a;
                            have_slot = true;
                            break;
                        }
                        if (!have_slot) {
                            slot = instr.info.phi.dest;
                            if (!temporals_map->array[slot.val].is_addressable) {
                                INTERNAL_ERROR("promoted PHI has no lowered slot owner");
                            }
                        }

                        // IR_temporal targets[1 + 64];
                        // size_t nt = 0;
                        // targets[nt++] = instr.info.phi.dest;
                        // for (size_t n = 0; n < instr.info.phi.args->len; n++) {
                        //     targets[nt++] = instr.info.phi.args->array[n].temp;
                        // }
                        // for (size_t n = 0; n < nt; n++) {
                        //     if (targets[n].val == slot.val) continue;
                        //     rewrite_map.array[targets[n].val].has_rewrite = true;
                        //     rewrite_map.array[targets[n].val].rewrite = slot;
                        // }
                        for(size_t n = 0; n < instr.info.phi.args->len; n++) {
                            if(instr.info.phi.args->array[n].temp.val == slot.val) continue;
                            rewrite_map.array[instr.info.phi.args->array[n].temp.val].has_rewrite = true;
                            rewrite_map.array[instr.info.phi.args->array[n].temp.val].rewrite = slot;
                        }
                        if(instr.info.phi.dest.val != slot.val) {
                            rewrite_map.array[instr.info.phi.dest.val].has_rewrite = true;
                            rewrite_map.array[instr.info.phi.dest.val].rewrite = slot;
                        }
                        break;
                    }

                    MIR_phi_arg_LIST args = MIR_phi_arg_LIST_new_with_capacity(instr.info.phi.args->len);
                    for (size_t n = 0; n < instr.info.phi.args->len; n++) {
                        MIR_phi_arg_LIST_push(&args, (MIR_phi_arg) {
                            .block_id = instr.info.phi.args->array[n].block,
                            .temp = instr.info.phi.args->array[n].temp.val
                        });
                    }
                    mir_emit(&mir_instrs, (MIR_instr) {
                        .type = MIR_TY__PHI,
                        .info.phi = {
                            .dest = instr.info.phi.dest.val,
                            .type = TC_typeid_deep_copy(&temporals_map->array[instr.info.phi.dest.val].type),
                            .args = args
                        }
                    });

                    // printf("   %%%lu = PHI { ", instr.info.phi.dest.val);
                    // for (size_t n = 0; n < instr.info.phi.args->len; n++) {
                    //     printf("<BLOCK %lu, %%%lu>", instr.info.phi.args->array[n].block,
                    //            instr.info.phi.args->array[n].temp.val);
                    //     if (n + 1 != instr.info.phi.args->len) printf(", ");
                    // }
                    // printf("}\n");
                    break;
                }
                case IR_INST_TY__DROP: {
                    const TC_typeid t = resolve_tc_type(instr.info.drop.type, nct, target_info, source_info, identifier_map);
                    mir_emit(&mir_instrs, (MIR_instr) {
                        .type = MIR_TY__DROP,
                        .info.drop = {
                            .dest = instr.info.drop.dest.val,
                            .type = TC_typeid_deep_copy(&t),
                            .drop_inside = t.ty == TC_TY_Primitive || t.ty == TC_TY_Ref || t.ty == TC_TY_Slice
                        }
                    });

                    // const SV type_sv = format_TC_typeid(&t, identifier_map, nct);
                    // printf("   DROP <"SV_format"> %%%lu ",SV_v_args(type_sv), instr.info.drop.dest.val);
                    // if (t.ty == TC_TY_Primitive) {
                    //     printf("[STACK]\n");
                    // } else if (t.ty == TC_TY_Ref || t.ty == TC_TY_Slice) {
                    //     printf("[STACK]\n");
                    // } else {
                    //     printf("[STACK+RAII]\n");
                    // }
                    break;
                }
                case IR_INST_TY__MOVE: {
                    // auto-deref, auto-ref, ...
                    const TC_typeid dest_t = temporals_map->array[instr.info.move.dest.val].type;
                    const TC_typeid src_t = temporals_map->array[instr.info.move.src.val].type;

                    //                     printf("[D] %%%lu = MOVE %%%lu\n", instr.info.move.dest.val, instr.info.move.src.val);

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
                        &temporals_map_values,
                        &promote_to_stack_temps,
                        target_info,
                        &mir_instrs
                    );
                    // printf("   %%%lu = MOVE %%%lu\n", instr.info.move.dest.val, out.val);

                    // do check, and emit move only if ref value, and if stack slot -> STORE
                    bool is_reg_val = true;
                    for (size_t n = 0; n < promote_to_stack_temps.len; n++) {
                        if (instr.info.move.dest.val == promote_to_stack_temps.array[n].val) {
                            is_reg_val = false;
                            break;
                        }
                    }

                    if (is_reg_val) {
                        mir_emit(&mir_instrs, (MIR_instr) {
                            .type = MIR_TY__MOVE,
                            .info.move = {
                                .dest = instr.info.move.dest.val,
                                .src = out.val,
                                .type = TC_typeid_deep_copy(&dest_t)
                            }
                        });
                    } else {
                        const SizeAlign dest_sa = calc_size_of(&dest_t, nct, target_info);
                        const bool val_on_stack = dest_sa.size > target_info->reg_size
                            || dest_t.ty == TC_TY_Array
                            || dest_t.ty == TC_TY_Struct
                            || dest_t.ty == TC_TY_Enum;
                        mir_emit(&mir_instrs, (MIR_instr) {
                            .type = MIR_TY__STORE,
                            .info.store = {
                                .ptr = instr.info.move.dest.val,
                                .val = out.val,
                                .is_val_stack = val_on_stack,
                                .val_type = TC_typeid_deep_copy(&dest_t)
                            }
                        });
                    }



                    break;
                }
                case IR_INST_TY__REF_OF: {

                    const TC_typeid src_t = temporals_map->array[instr.info.ref_of.src.val].type;
                    const TC_typeid dest_t = temporals_map->array[instr.info.ref_of.dest.val].type;


                    if (instr_nth + 1 < block.instructions.len && block.instructions.array[instr_nth+1].ty == IR_INST_TY__MATCH_META__TARGET && src_t.ty == TC_TY_Ref) {
                        // emit MOVE
                        mir_emit(&mir_instrs, (MIR_instr) {
                            .type = MIR_TY__MOVE,
                            .info.move = {
                                .dest = instr.info.ref_of.dest.val,
                                .src = instr.info.ref_of.src.val,
                                .type = TC_typeid_deep_copy(&dest_t)
                            }
                        });
                        // printf("   %%%lu = MOVE %%%lu\n", instr.info.ref_of.dest.val, instr.info.ref_of.src.val);
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

                    mir_emit(&mir_instrs, (MIR_instr) {
                        .type = MIR_TY__REF_OF,
                        .info.ref_of = {
                            .dest = instr.info.ref_of.dest.val,
                            .src = instr.info.ref_of.src.val
                        }
                    });
                    // printf("   %%%lu = REF_OF %%%lu\n", instr.info.ref_of.dest.val, instr.info.ref_of.src.val);

                    break;
                }
                case IR_INST_TY__VAR_DECL: {
                    temporals_map->array[instr.info.var_decl.dest.val].is_addressable = true;
                    const TC_typeid t = temporals_map->array[instr.info.var_decl.dest.val].type;
                    const TC_typeid original_t = resolve_tc_type(instr.info.var_decl.type, nct, target_info,
                                                                 source_info, identifier_map);
                    if (!is_type_eq(&t, &original_t)) {
                        INTERNAL_ERROR("should not happen")
                    }
                    // const SV t_sv = format_TC_typeid(&t, identifier_map, nct);
                    // char* value_vs_stack = "value";
                    bool value = true;
                    for (size_t n = 0; n < promote_to_stack_temps.len; n++) {
                        if (promote_to_stack_temps.array[n].val == instr.info.var_decl.dest.val) {
                            // value_vs_stack = "stack";
                            value = false;
                            break;
                        }
                    }
                    if (!value) {
                        const bool is_fn_arg =
                            instr.info.var_decl.dest.val >= 1 &&
                            instr.info.var_decl.dest.val <= fn.args.len;
                        const SizeAlign sa = calc_size_of(&t, nct, target_info);
                        const bool is_aggregate_or_fat_ptr =
                            sa.size > target_info->reg_size
                            || t.ty == TC_TY_Array
                            || t.ty == TC_TY_Struct
                            || t.ty == TC_TY_Enum
                            || t.ty == TC_TY_Slice
                            || t.ty == TC_TY_List;
                        if (is_fn_arg && is_aggregate_or_fat_ptr) {
                            // Incoming function argument %v(1..fn.args.len) is already a pointer
                            // to the caller-provided stack buffer / aggregate; do not overwrite it
                            // with an uninitialized MIR_TY__ALLOC_STACK.
                        } else if (is_fn_arg && !is_aggregate_or_fat_ptr) {
                            // Scalar parameter promoted to stack (e.g. its reference was taken):
                            // save the incoming register value, allocate the stack slot, and store it.
                            const uint64_t incoming_val = highest_temporal++;
                            TC_Entry entry_copy = temporals_map->array[instr.info.var_decl.dest.val];
                            entry_copy.type = TC_typeid_deep_copy(&t);
                            entry_copy.is_addressable = false;
                            while (temporals_map->len <= incoming_val) {
                                TC_Entry_LIST_push(temporals_map, (TC_Entry){
                                    .known_temporal = false
                                });
                            }
                            temporals_map->array[incoming_val] = entry_copy;
                            mir_emit(&mir_instrs, (MIR_instr) {
                                .type = MIR_TY__MOVE,
                                .info.move = {
                                    .dest = incoming_val,
                                    .src = instr.info.var_decl.dest.val,
                                    .type = TC_typeid_deep_copy(&t)
                                }
                            });
                            mir_emit(&mir_instrs, (MIR_instr) {
                                .type = MIR_TY__ALLOC_STACK,
                                .info.alloc_stack = {
                                    .dest = instr.info.var_decl.dest.val,
                                    .type = TC_typeid_deep_copy(&t),
                                }
                            });
                            mir_emit(&mir_instrs, (MIR_instr) {
                                .type = MIR_TY__STORE,
                                .info.store = {
                                    .ptr = instr.info.var_decl.dest.val,
                                    .val = incoming_val,
                                    .is_val_stack = false,
                                    .val_type = TC_typeid_deep_copy(&t)
                                }
                            });
                        } else {
                            mir_emit(&mir_instrs, (MIR_instr) {
                                .type = MIR_TY__ALLOC_STACK,
                                .info.alloc_stack = {
                                    .dest = instr.info.var_decl.dest.val,
                                    .type = TC_typeid_deep_copy(&temporals_map->array[instr.info.var_decl.dest.val].type),
                                }
                            });
                        }
                    }
                    // printf("   VAR_DECL %%%lu {on %s} <"SV_format">\n", instr.info.var_decl.dest.val, value_vs_stack, SV_v_args(t_sv));
                    break;
                }
                case IR_INST_TY__STORE_PTR: {
                    // 1. The forward pass already processed INDEX_PTR, so the type is known!
                    TC_typeid ptr_ty = temporals_map->array[instr.info.store_ptr.ptr.val].type;

                    // 2. What type does this pointer hold? (e.g. Ref<Char> -> Char)
                    if (ptr_ty.ty != TC_TY_Ref) {
                        INTERNAL_ERROR();
                    }
                    // TC_typeid expected_ty = *ptr_ty.info.contained; // unwrap_ref_once(ptr_ty);

                    const TC_typeid value_ty = TC_typeid_deep_copy(&temporals_map->array[instr.info.store_ptr.value.val].type);
                    const TC_typeid expected_ty = TC_typeid_deep_copy(ptr_ty.info.contained);

                    // 3. Coerce the RHS value (e.g. Int32 '72') to match (Char)
                    IR_temporal coerced_val = perf_autos(
                        &instr.info.store_ptr.value,
                        &value_ty,
                        &expected_ty,
                        &highest_temporal,
                        identifier_map,
                        nct,
                        temporals_map,
                        source_info,
                        &fn.error_info_list.array[instr.error_info_index].info.store_ptr.expr.sv,
                        &temporals_map_values,
                        &promote_to_stack_temps,
                        target_info,
                        &mir_instrs
                    );

                    mir_emit(&mir_instrs, (MIR_instr) {
                        .type = MIR_TY__STORE,
                        .info.store = {
                            .ptr = instr.info.store_ptr.ptr.val,
                            .val = coerced_val.val,
                            .is_val_stack = is_type_on_stack(&expected_ty, nct, target_info), // coerced_val.val != instr.info.store_ptr.value.val,
                            .val_type = TC_typeid_deep_copy(&expected_ty)
                        }
                    });
                    // printf("   STORE INTO %%%lu VALUE %%%lu\n",
                    //        instr.info.store_ptr.ptr.val,
                    //        coerced_val.val);
                    break;
                }
                case IR_INST_TY__EXPLICIT_CAST: {
                    // can_be_explicit_casted
                    const TC_typeid src = TC_typeid_deep_copy(&temporals_map->array[instr.info.explicit_cast.src.val].type);
                    const TC_typeid r_cast_t = resolve_tc_type(instr.info.explicit_cast.type, nct, target_info, source_info, identifier_map);
                    const TC_typeid cast_t = TC_typeid_deep_copy(&r_cast_t);

                    const SV cast_t_sv = format_TC_typeid(&cast_t, identifier_map, nct);

                    if (!can_be_explicit_casted(&src, &cast_t)) {
                        const SV whole = fn.error_info_list.array[instr.error_info_index].info.explicit_cast.whole.sv;
                        const SV expr = fn.error_info_list.array[instr.error_info_index].info.explicit_cast.expr.sv;
                        // const SV type = fn.error_info_list.array[instr.error_info_index].info.explicit_cast.type.sv;

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
                    const TC_typeid dest_t = TC_typeid_deep_copy(&temporals_map->array[instr.info.explicit_cast.dest.val].type);
                    const SV dest_t_sv = format_TC_typeid(&dest_t, identifier_map, nct);

                    uint64_t out_temporal = instr.info.explicit_cast.dest.val;
                    if (!is_type_eq(&dest_t, &cast_t)) {
                        out_temporal = highest_temporal++;
                        if (out_temporal + 1 > temporals_map->cap) {
                            TC_Entry_LIST_inflate(temporals_map, out_temporal + 5);
                            temporals_map->array[out_temporal] = (TC_Entry) {
                                .known_temporal = true,
                                .type = TC_typeid_deep_copy(&cast_t),
                                .is_value_known = false,
                                .is_addressable = false
                            };
                        }
                        if (!can_be_implicit_casted(&cast_t, &dest_t)) {
                            const SV whole = fn.error_info_list.array[instr.error_info_index].info.explicit_cast.whole.
                                    sv;
                            // const SV expr = fn.error_info_list.array[instr.error_info_index].info.explicit_cast.expr.sv;
                            // const SV type = fn.error_info_list.array[instr.error_info_index].info.explicit_cast.type.sv;

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

                    // printf("   %%%lu = CAST<"SV_format">(%%%lu) [EXPLICIT]\n", out_temporal, SV_v_args(cast_t_sv), instr.info.explicit_cast.src.val);
                    mir_emit(&mir_instrs, (MIR_instr) {
                        .type = MIR_TY__CAST,
                        .info.cast = {
                            .dest = out_temporal,
                            .src = instr.info.explicit_cast.src.val,
                            .from_type = TC_typeid_deep_copy(&src), // temporals_map->array[instr.info.explicit_cast.src.val].type,
                            .to_type = TC_typeid_deep_copy(&cast_t), // temporals_map->array[instr.info.explicit_cast.src.val].type,
                            .cast_kind = MIR_CAST_EXPLICIT
                        }
                    });

                    if (out_temporal != instr.info.explicit_cast.dest.val) {
                        // printf("   %%%lu = CAST<", instr.info.explicit_cast.dest.val);
                        // print_TC_typeid(&dest_t, identifier_map, nct);
                        // printf(">(%%%lu) [IMPLICIT]\n", out_temporal);


                        // printf("   %%%lu = CAST<"SV_format">(%%%lu) [IMPLICIT]\n", instr.info.explicit_cast.dest.val, SV_v_args(dest_t_sv), out_temporal);
                        mir_emit(&mir_instrs, (MIR_instr) {
                            .type = MIR_TY__CAST,
                            .info.cast = {
                                .dest = instr.info.explicit_cast.dest.val,
                                .src = out_temporal,
                                .from_type = TC_typeid_deep_copy(&cast_t), // temporals_map->array[out_temporal].type,
                                .to_type = TC_typeid_deep_copy(&dest_t), // temporals_map->array[instr.info.explicit_cast.dest.val].type,
                                .cast_kind = MIR_CAST_IMPLICIT
                            }
                        });


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
                    const SV t_sv = format_TC_typeid(&dest_t, identifier_map, nct);

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

                    // printf("   %%%lu = CONST_INT <"SV_format"> (%li)\n", instr.info.const_int.dest.val, SV_v_args(t_sv), instr.info.const_int.val);
                    mir_emit(&mir_instrs, (MIR_instr) {
                        .type = MIR_TY__CONST_INT,
                        .info.const_int = {
                            .dest = instr.info.const_int.dest.val,
                            .val = instr.info.const_int.val,
                            .type = TC_typeid_deep_copy(&temporals_map->array[instr.info.const_int.dest.val].type)
                        }
                    });

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

                    // printf("   %%%lu = CONST_FLOAT <"SV_format"> (%lf)\n", instr.info.const_float.dest.val, SV_v_args(t_sv), instr.info.const_float.val);
                    mir_emit(&mir_instrs, (MIR_instr) {
                        .type = MIR_TY__CONST_FLOAT,
                        .info.const_float = {
                            .dest = instr.info.const_float.dest.val,
                            .val = instr.info.const_float.val,
                            .type = TC_typeid_deep_copy(&temporals_map->array[instr.info.const_float.dest.val].type)
                        }
                    });



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
                    const SV t_sv = format_TC_typeid(&dest_t, identifier_map, nct);

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

                    switch (dest_t.ty) {
                        case TC_TY_Slice: {
                            const uint64_t dest = instr.info.const_string.dest.val;
                            const uint64_t len_val = highest_temporal++;
                            mir_emit(&mir_instrs, (MIR_instr) {
                                .type = MIR_TY__CONST_INT,
                                .info.const_int = {
                                    .dest = len_val,
                                    .val = (int64_t)val.len,
                                    .type = (TC_typeid){ .ty = TC_TY_Primitive, .info.primitive = TC_Primitive__Uint32 }
                                }
                            });
                            const uint64_t raw_ptr_temp = highest_temporal++;
                            mir_emit(&mir_instrs, (MIR_instr) {
                                .type = MIR_TY__LOAD_PTR_TO_CONST_STRING,
                                .info.load_ptr_const_string = {
                                    .dest = raw_ptr_temp,
                                    .str_val = val,
                                }
                            });
                            // alloc Slice<Char> stack slot
                            mir_emit(&mir_instrs, (MIR_instr) {
                                .type = MIR_TY__ALLOC_STACK,
                                .info.alloc_stack = {
                                    .dest = dest,
                                    .type = TC_typeid_deep_copy(&dest_t),
                                }
                            });
                            // Store ptr (Ref<Char>) at offset 0
                            TC_typeid char_ty = (TC_typeid){ .ty = TC_TY_Primitive, .info.primitive = TC_Primitive__Char };
                            TC_typeid char_ptr_ty = (TC_typeid){ .ty = TC_TY_Ref, .info.contained = TC_typeid_new_copy(&char_ty) };
                            mir_emit(&mir_instrs, (MIR_instr) {
                                .type = MIR_TY__STORE,
                                .info.store = {
                                    .ptr = dest,
                                    .val = raw_ptr_temp,
                                    .is_val_stack = false,
                                    .val_type = char_ptr_ty
                                }
                            });
                            // Calculate pointer to .len field at offset ptr_size (8)
                            const uint64_t len_ptr_temp = highest_temporal++;
                            mir_emit(&mir_instrs, (MIR_instr) {
                                .type = MIR_TY__CALC_PTR,
                                .info.calc_ptr = {
                                    .dest = len_ptr_temp,
                                    .base = dest,
                                    .offset = target_info->ptr_size,
                                    .scale = 4,
                                    .index = 0,
                                    .has_index = false
                                }
                            });
                            // store len (Uint32) at offset ptr_size
                            mir_emit(&mir_instrs, (MIR_instr) {
                                .type = MIR_TY__STORE,
                                .info.store = {
                                    .ptr = len_ptr_temp,
                                    .val = len_val,
                                    .is_val_stack = false,
                                    .val_type = (TC_typeid){ .ty = TC_TY_Primitive, .info.primitive = TC_Primitive__Uint32 }
                                }
                            });
                            break;
                        }
                        case TC_TY_Array: {
                            const uint64_t dest = instr.info.const_string.dest.val;
                            const uint64_t raw_ptr_temp = highest_temporal++;
                            mir_emit(&mir_instrs, (MIR_instr) {
                                .type = MIR_TY__LOAD_PTR_TO_CONST_STRING,
                                .info.load_ptr_const_string = {
                                    .dest = raw_ptr_temp,
                                    .str_val = val,
                                }
                            });
                            mir_emit(&mir_instrs, (MIR_instr) {
                                .type = MIR_TY__ALLOC_STACK,
                                .info.alloc_stack = {
                                    .dest = dest,
                                    .type = TC_typeid_deep_copy(&dest_t),
                                }
                            });
                            // blit N bytes from $strN (raw_ptr_temp) into dest stack buffer
                            mir_emit(&mir_instrs, (MIR_instr) {
                                .type = MIR_TY__STORE,
                                .info.store = {
                                    .ptr = dest,
                                    .val = raw_ptr_temp,
                                    .is_val_stack = true,
                                    .val_type = TC_typeid_deep_copy(&dest_t)
                                }
                            });
                            break;
                        }
                        case TC_TY_List: {
                            const uint64_t dest = instr.info.const_string.dest.val;
                            const TC_typeid char_ty = (TC_typeid){ .ty = TC_TY_Primitive, .info.primitive = TC_Primitive__Char };
                            const TC_typeid u32_ty = (TC_typeid){ .ty = TC_TY_Primitive, .info.primitive = TC_Primitive__Uint32 };
                            // len_val = val.len (Uint32)
                            const uint64_t len_val = highest_temporal++;
                            mir_emit(&mir_instrs, (MIR_instr) {
                                .type = MIR_TY__CONST_INT,
                                .info.const_int = {
                                    .dest = len_val,
                                    .val = (int64_t)val.len,
                                    .type = u32_ty
                                }
                            });
                            // raw_ptr_temp = $strN
                            const uint64_t raw_ptr_temp = highest_temporal++;
                            mir_emit(&mir_instrs, (MIR_instr) {
                                .type = MIR_TY__LOAD_PTR_TO_CONST_STRING,
                                .info.load_ptr_const_string = {
                                    .dest = raw_ptr_temp,
                                    .str_val = val,
                                }
                            });
                            // alloc heap buffer for List<Char> and copy $strN bytes into it
                            const uint64_t heap_ptr_temp = highest_temporal++;
                            TC_typeid arr_ty = (TC_typeid){
                                .ty = TC_TY_Array,
                                .info.t_array = {
                                    .contained = TC_typeid_new_copy(&char_ty),
                                    .array_size = (uint32_t)val.len
                                }
                            };
                            mir_emit(&mir_instrs, (MIR_instr) {
                                .type = MIR_TY__ALLOC_HEAP,
                                .info.alloc_heap = {
                                    .dest = heap_ptr_temp,
                                    .type = TC_typeid_deep_copy(&arr_ty),
                                    .use_size_temp = false,
                                    .size = 0
                                }
                            });
                            mir_emit(&mir_instrs, (MIR_instr) {
                                .type = MIR_TY__STORE,
                                .info.store = {
                                    .ptr = heap_ptr_temp,
                                    .val = raw_ptr_temp,
                                    .is_val_stack = true,
                                    .val_type = arr_ty
                                }
                            });
                            // alloc List<Char> struct ({ ptr; len; cap; }) on stack
                            mir_emit(&mir_instrs, (MIR_instr) {
                                .type = MIR_TY__ALLOC_STACK,
                                .info.alloc_stack = {
                                    .dest = dest,
                                    .type = TC_typeid_deep_copy(&dest_t)
                                }
                            });
                            // store heap_ptr_temp at offset 0 (ptr)
                            TC_typeid char_ptr_ty = (TC_typeid){ .ty = TC_TY_Ref, .info.contained = TC_typeid_new_copy(&char_ty) };
                            mir_emit(&mir_instrs, (MIR_instr) {
                                .type = MIR_TY__STORE,
                                .info.store = {
                                    .ptr = dest,
                                    .val = heap_ptr_temp,
                                    .is_val_stack = false,
                                    .val_type = char_ptr_ty
                                }
                            });
                            // store len_val at offset ptr_size (len)
                            const uint64_t len_ptr_temp = highest_temporal++;
                            mir_emit(&mir_instrs, (MIR_instr) {
                                .type = MIR_TY__CALC_PTR,
                                .info.calc_ptr = {
                                    .dest = len_ptr_temp,
                                    .base = dest,
                                    .offset = target_info->ptr_size,
                                    .scale = 4,
                                    .index = 0,
                                    .has_index = false
                                }
                            });
                            mir_emit(&mir_instrs, (MIR_instr) {
                                .type = MIR_TY__STORE,
                                .info.store = {
                                    .ptr = len_ptr_temp,
                                    .val = len_val,
                                    .is_val_stack = false,
                                    .val_type = u32_ty
                                }
                            });
                            // store len_val at offset ptr_size + 4 (cap)
                            const uint64_t cap_ptr_temp = highest_temporal++;
                            mir_emit(&mir_instrs, (MIR_instr) {
                                .type = MIR_TY__CALC_PTR,
                                .info.calc_ptr = {
                                    .dest = cap_ptr_temp,
                                    .base = dest,
                                    .offset = target_info->ptr_size + 4, //* 2,
                                    .scale = 4,
                                    .index = 0,
                                    .has_index = false
                                }
                            });
                            mir_emit(&mir_instrs, (MIR_instr) {
                                .type = MIR_TY__STORE,
                                .info.store = {
                                    .ptr = cap_ptr_temp,
                                    .val = len_val,
                                    .is_val_stack = false,
                                    .val_type = u32_ty
                                }
                            });
                            break;
                        }
                        default: {
                            UNREACHABLE("should be rejected in typechecking")
                        }

                    }




                    // printf("   %%%lu = CONST_STRING <"SV_format"> (\"", instr.info.const_string.dest.val,
                    //        SV_v_args(t_sv));
                    // for (size_t i = 0; i < val.len; i++) {
                    //     unsigned char c = val.start[i];
                    //     if (c == '\n') printf("\\n");
                    //     else if (c == '\t') printf("\\t");
                    //     else if (c == '\r') printf("\\r");
                    //     else if (c == '\\') printf("\\\\");
                    //     else if (isprint(c)) putchar(c);
                    //     else printf("\\x%02X", c);
                    // }
                    // printf("\")\n");
                    break;
                }
                case IR_INST_TY__OP_ADD:
                case IR_INST_TY__OP_SUB:
                case IR_INST_TY__OP_DIV:
                case IR_INST_TY__OP_MUL:
                case IR_INST_TY__OP_POW: {
                    bool is_error_out;
                    bool is_out_error_incompatible_number_relation;

                    const TC_typeid src1_t = TC_typeid_deep_copy(&temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].type);
                    const TC_typeid src2_t = TC_typeid_deep_copy(&temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].type);

                    const DoubleIR_temporal_and_TC_typeid res = perf_numeric_promotion(
                        &instr.info.math_cmp_boolean_op.src1,
                        &temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].type,
                        &instr.info.math_cmp_boolean_op.src2,
                        &temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].type,
                        &highest_temporal,
                        &is_error_out,
                        &is_out_error_incompatible_number_relation,
                        temporals_map,
                        &temporals_map_values,
                        &promote_to_stack_temps,
                        nct,
                        target_info,
                        identifier_map,
                        &mir_instrs
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
                                &src1_t, // &temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].type,
                                identifier_map,
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
                                &src2_t,
                                // &temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].type,
                                identifier_map,
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
                            &src1_t,
                            // &temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].type,
                            identifier_map, nct);
                        const SV b_type = format_TC_typeid(
                            &src2_t,
                            // &temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].type,
                            identifier_map, nct);

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

                    MIR_binop_kind mik;
                    switch (instr.ty) {
                        case IR_INST_TY__OP_ADD: {
                            mik = MIR_BINOP_ADD;
                            break;
                        }
                        case IR_INST_TY__OP_SUB: {
                            mik = MIR_BINOP_SUB;
                            break;
                        }
                        case IR_INST_TY__OP_DIV: {
                            mik = MIR_BINOP_DIV;
                            break;
                        }
                        case IR_INST_TY__OP_MUL: {
                            mik = MIR_BINOP_MUL;
                            break;
                        }
                        case IR_INST_TY__OP_POW: {
                            mik = MIR_BINOP_POW;
                            break;
                        }
                        default: INTERNAL_ERROR()
                    }

                    mir_emit(&mir_instrs, (MIR_instr) {
                        .type = MIR_TY__OP_BINOP,
                        .info.binop = {
                            .dest = out_temporal,
                            .a = res.a.val,
                            .b = res.b.val,
                            .type = TC_typeid_deep_copy(&res.t),
                            .op = mik
                        }
                    });
                    // printf("   %%%lu = ", out_temporal);
                    // switch (instr.ty) {
                    //     case IR_INST_TY__OP_ADD: {
                    //         printf("OP_ADD");
                    //         break;
                    //     }
                    //     case IR_INST_TY__OP_SUB: {
                    //         printf("OP_SUB");
                    //         break;
                    //     }
                    //     case IR_INST_TY__OP_DIV: {
                    //         printf("OP_DIV");
                    //         break;
                    //     }
                    //     case IR_INST_TY__OP_MUL: {
                    //         printf("OP_MUL");
                    //         break;
                    //     }
                    //     case IR_INST_TY__OP_POW: {
                    //         printf("OP_POW");
                    //         break;
                    //     }
                    //     default: INTERNAL_ERROR()
                    // }


                    // printf(" <");
                    // print_TC_typeid(&res.t, identifier_map, nct);
                    // printf("> %%%lu, %%%lu\n", res.a.val, res.b.val);

                    // printf(" <"SV_format"> %%%lu, %%%lu\n", SV_v_args(res_t_sv), res.a.val, res.b.val);

                    if (out_temporal != instr.info.math_cmp_boolean_op.dest.val) {

                        if (out_temporal + 1 > temporals_map->cap) {
                            TC_Entry_LIST_inflate(temporals_map, out_temporal+5);
                        }

                        temporals_map->array[out_temporal].type = TC_typeid_deep_copy(&res.t);

                        // printf("   %%%lu = CAST<", instr.info.math_cmp_boolean_op.dest.val);
                        // print_TC_typeid(&temporals_map->array[instr.info.math_cmp_boolean_op.dest.val].type, identifier_map, nct);
                        // printf(">(%%%lu) [IMPLICIT]\n", out_temporal);

                        // printf("   %%%lu = CAST<"SV_format">(%%%lu) [IMPLICIT]\n", instr.info.math_cmp_boolean_op.dest.val, SV_v_args(dest_t_sv), out_temporal);
                        mir_emit(&mir_instrs, (MIR_instr) {
                            .type = MIR_TY__CAST,
                            .info.cast = {
                                .dest = instr.info.math_cmp_boolean_op.dest.val,
                                .src = out_temporal,
                                .from_type = TC_typeid_deep_copy(&temporals_map->array[out_temporal].type),
                                .to_type = TC_typeid_deep_copy(&dest_t),
                                .cast_kind = MIR_CAST_IMPLICIT
                            }
                        });

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
                            // printf("   STORE_PTR INTO %%%lu VALUE %%%lu\n", instr.info.math_cmp_boolean_op.dest.val, out_temporal);
                            mir_emit(&mir_instrs, (MIR_instr) {
                                .type = MIR_TY__STORE,
                                .info.store = {
                                    .ptr = instr.info.math_cmp_boolean_op.dest.val,
                                    .val = out_temporal,
                                    .is_val_stack = false,
                                    .val_type = TC_typeid_deep_copy(&res.t)
                                }
                            });
                        }
                    }
                    break;
                }
                // case IR_INST_TY__OP_OR:
                // case IR_INST_TY__OP_AND: {
                //     const TC_typeid src1_t = temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].type;
                //     const TC_typeid src2_t = temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].type;
                //     const TC_typeid dest_t = temporals_map->array[instr.info.math_cmp_boolean_op.dest.val].type;
                //
                //     const IR_temporal src1_it = perf_bool(&instr.info.math_cmp_boolean_op.src1, &src1_t,
                //                                           &highest_temporal, temporals_map, &promote_to_stack_temps, nct, target_info);
                //     const IR_temporal src2_it = perf_bool(&instr.info.math_cmp_boolean_op.src2, &src2_t,
                //                                           &highest_temporal, temporals_map, &promote_to_stack_temps, nct, target_info);
                //
                //     static TC_typeid bool_t = {
                //         .ty = TC_TY_Primitive,
                //         .info.primitive = TC_Primitive__Bool,
                //         // .full_typeuse_span = {.sv = {"Bool", 4}}
                //     };
                //
                //     uint64_t out_temporal = instr.info.math_cmp_boolean_op.dest.val;
                //     if (dest_t.ty != TC_TY_Primitive || dest_t.info.primitive != TC_Primitive__Bool) {
                //         out_temporal = highest_temporal++;
                //
                //         if (out_temporal + 1 > temporals_map->cap) {
                //             TC_Entry_LIST_inflate(temporals_map, out_temporal + 5);
                //         }
                //         temporals_map->array[out_temporal] = (TC_Entry){
                //             .type = dest_t,
                //             .known_temporal = true,
                //             .is_value_known = false,
                //             .is_addressable = false,
                //         };
                //
                //         if (!can_be_implicit_casted(&bool_t, &dest_t)) {
                //             INTERNAL_ERROR("TYPE ERROR HERE");
                //         } else {
                //             printf("   [D] implicit type conversion from ");
                //             print_TC_typeid(&bool_t, identifier_map, nct);
                //             printf(" to ");
                //             print_TC_typeid(&dest_t, identifier_map, nct);
                //             printf(" is possible \n");
                //         }
                //     }
                //
                //     printf("   %%%lu = ", out_temporal);
                //     switch (instr.ty) {
                //         case IR_INST_TY__OP_OR: {
                //             printf("OP_OR");
                //             break;
                //         }
                //         case IR_INST_TY__OP_AND: {
                //             printf("OP_AND");
                //             break;
                //         }
                //         default: INTERNAL_ERROR()
                //     }
                //
                //     printf(" <");
                //     print_TC_typeid(&bool_t, identifier_map, nct);
                //     printf("> %%%lu, %%%lu\n", src1_it.val, src2_it.val);
                //
                //     if (out_temporal != instr.info.math_cmp_boolean_op.dest.val) {
                //         printf("   %%%lu = CAST<", instr.info.math_cmp_boolean_op.dest.val);
                //         print_TC_typeid(&dest_t, identifier_map, nct);
                //         printf(">(%%%lu) [IMPLICIT]\n", out_temporal);
                //     }
                //
                //     break;
                // }
                case IR_INST_TY__OP_CMP_EQ:
                case IR_INST_TY__OP_CMP_NOT_EQ:
                case IR_INST_TY__OP_CMP_LESS:
                case IR_INST_TY__OP_CMP_LESS_EQ:
                case IR_INST_TY__OP_CMP_GREATER:
                case IR_INST_TY__OP_CMP_GREATER_EQ: {
                    // convert to numerical
                    bool is_error_out;
                    bool is_out_error_incompatible_number_relation;

                    const TC_typeid src1_t = TC_typeid_deep_copy(&temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].type);
                    const TC_typeid src2_t = TC_typeid_deep_copy(&temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].type);

                    const DoubleIR_temporal_and_TC_typeid res = perf_numeric_promotion(
                        &instr.info.math_cmp_boolean_op.src1,
                        &temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].type,
                        &instr.info.math_cmp_boolean_op.src2,
                        &temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].type,
                        &highest_temporal,
                        &is_error_out,
                        &is_out_error_incompatible_number_relation,
                        temporals_map,
                        &temporals_map_values,
                        &promote_to_stack_temps,
                        nct,
                        target_info,
                        identifier_map,
                        &mir_instrs
                    );

                    // RESULT OF OPERATION IS BOOL TYPE
                    const TC_typeid res_t = (TC_typeid){
                        .ty = TC_TY_Primitive,
                        .info.primitive = TC_Primitive__Bool,
                        // .full_typeuse_span = (TokenSpan){.sv = SV_from_str_len("Bool", 4)}
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
                                &src1_t,
                                // &temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].type,
                                identifier_map,
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
                                &src2_t,
                                // &temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].type,
                                identifier_map,
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
                            &src1_t,
                            // &temporals_map->array[instr.info.math_cmp_boolean_op.src1.val].type,
                            identifier_map, nct);
                        const SV b_type = format_TC_typeid(
                            &src2_t,
                            // &temporals_map->array[instr.info.math_cmp_boolean_op.src2.val].type,
                            identifier_map, nct);

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
                    const TC_typeid dest_t = TC_typeid_deep_copy(&temporals_map->array[instr.info.math_cmp_boolean_op.dest.val].type);
                    const SV dest_t_sv = format_TC_typeid(
                        &dest_t, identifier_map, nct);

                    uint64_t out_temporal = instr.info.math_cmp_boolean_op.dest.val;
                    if (
                        dest_t.ty != TC_TY_Primitive ||
                        dest_t.info.primitive !=
                        TC_Primitive__Bool
                    ) {
                        out_temporal = highest_temporal++;


                        if (!can_be_implicit_casted(
                            &res_t, &dest_t)) {
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
                                .type = dest_t, //temporals_map->array[instr.info.math_cmp_boolean_op.dest.val].type,
                                .is_value_known = false,
                                .is_addressable = false,
                            };
                            // print_TC_typeid(&res_t, identifier_map, nct);
                            // printf(" to ");
                            // print_TC_typeid(&temporals_map->array[instr.info.math_cmp_boolean_op.dest.val].type, identifier_map, nct);
                            // printf(" is possible \n");
                        }
                    }



                    // printf("   %%%lu = ", out_temporal);
                    // switch (instr.ty) {
                    //     case IR_INST_TY__OP_CMP_EQ: {
                    //         printf("CMP_EQ");
                    //         break;
                    //     }
                    //     case IR_INST_TY__OP_CMP_NOT_EQ: {
                    //         printf("CMP_NOT_EQ");
                    //         break;
                    //     }
                    //     case IR_INST_TY__OP_CMP_LESS: {
                    //         printf("CMP_LESS");
                    //         break;
                    //     }
                    //     case IR_INST_TY__OP_CMP_LESS_EQ: {
                    //         printf("CMP_LESS_EQ");
                    //         break;
                    //     }
                    //     case IR_INST_TY__OP_CMP_GREATER: {
                    //         printf("CMP_GREATER");
                    //         break;
                    //     }
                    //     case IR_INST_TY__OP_CMP_GREATER_EQ: {
                    //         printf("CMP_GREATER_EQ");
                    //         break;
                    //     }
                    //     default: INTERNAL_ERROR()
                    // }
                    // printf(" <"SV_format"> %%%lu, %%%lu\n", SV_v_args(res_t_sv), res.a.val, res.b.val);
                    MIR_cmp_kind cmp_kind;
                    switch (instr.ty) {
                        case IR_INST_TY__OP_CMP_EQ: {
                            cmp_kind = MIR_CMP_EQ;
                            break;
                        }
                        case IR_INST_TY__OP_CMP_NOT_EQ: {
                            cmp_kind = MIR_CMP_NOT_EQ;
                            break;
                        }
                        case IR_INST_TY__OP_CMP_LESS: {
                            cmp_kind = MIR_CMP_LESS;
                            break;
                        }
                        case IR_INST_TY__OP_CMP_LESS_EQ: {
                            cmp_kind = MIR_CMP_LESS_EQ;
                            break;
                        }
                        case IR_INST_TY__OP_CMP_GREATER: {
                            cmp_kind = MIR_CMP_GREATER;
                            break;
                        }
                        case IR_INST_TY__OP_CMP_GREATER_EQ: {
                            cmp_kind = MIR_CMP_GREATER_EQ;
                            break;
                        }
                        default: INTERNAL_ERROR()
                    }
                    mir_emit(&mir_instrs, (MIR_instr) {
                        .type = MIR_TY__OP_CMP,
                        .info.cmp = {
                            .dest = out_temporal,
                            .a = res.a.val,
                            .b = res.b.val,
                            .type = TC_typeid_deep_copy(&res.t),
                            .cmp_kind = cmp_kind
                        }
                    });

                    if (out_temporal != instr.info.math_cmp_boolean_op.dest.val) {
                        // printf("   %%%lu = CAST<"SV_format">(%%%lu) [IMPLICIT]\n", instr.info.math_cmp_boolean_op.dest.val, SV_v_args(dest_t_sv), out_temporal);
                        mir_emit(&mir_instrs, (MIR_instr) {
                            .type = MIR_TY__CAST,
                            .info.cast = {
                                .dest = instr.info.math_cmp_boolean_op.dest.val,
                                .src = out_temporal,
                                .from_type = TC_typeid_deep_copy(&temporals_map->array[out_temporal].type),
                                .to_type = TC_typeid_deep_copy(&dest_t), //temporals_map->array[instr.info.math_cmp_boolean_op.dest.val].type,
                                .cast_kind = MIR_CAST_IMPLICIT
                            }
                        });
                    }
                    break;
                }
                // case IR_INST_TY__INDEX_PTR: {
                //     // 1. What are we indexing into? (e.g., %11 which is List<Char>)
                //     const TC_typeid source_list_ty = temporals_map->array[instr.info.index_ptr.array_ptr.val].type;
                //
                //
                //     // const IR_temporal_and_TC_type list = perf_auto_deref(&source_list_ty, &instr.info.index_ptr.array_ptr, &highest_temporal, temporals_map);
                //     // if (source_list_ty.ty != TC_TY_Ref) {
                //     //     // type error
                //     //     const SV source_list_ty_sv = format_TC_typeid(&source_list_ty, identifier_map, nct);
                //     //     char r_msg[1024] = {0};
                //     //     int len = snprintf(r_msg, sizeof(r_msg), "type "C_BOLD""SV_format""C_RESET" cannot be indexed",
                //     //                        SV_v_args(source_list_ty_sv));
                //     //     const SV msg = SV_from_str_len(r_msg, len);
                //     //     type_error(&source_info->source, &source_info->source_filename,
                //     //                &fn.error_info_list.array[instr.error_info_index].info.index_array.index_expr.sv,
                //     //                &msg);
                //     //     exit(1);
                //     // }
                //     const IR_temporal_and_TC_type list = perf_auto_deref(&source_list_ty, &instr.info.index_ptr.array_ptr, &highest_temporal, temporals_map, &promote_to_stack_temps, nct, target_info, &mir_instrs);
                //     const SV list_ty_sv = format_TC_typeid(&list.type, identifier_map, nct);
                //
                //     // const IR_temporal_and_TC_type list = (IR_temporal_and_TC_type) {
                //     //     .temporal = instr.info.index_ptr.array_ptr,
                //     //     .type = *source_list_ty.info.contained
                //     // };
                //     // const SV list_ty_sv = format_TC_typeid(&list.type, identifier_map, nct);
                //
                //     // 2. Extract the inner element type ('Char')
                //     // TODO: assert array/list size check
                //     TC_typeid* element_ty;
                //     switch (list.type.ty) {
                //         case TC_TY_Void: INTERNAL_ERROR()
                //         case TC_TY_Primitive:
                //         case TC_TY_Struct:
                //         case TC_TY_Box:
                //         case TC_TY_Ref:
                //         case TC_TY_External_Type:
                //         case TC_TY_Enum: {
                //             // type error
                //             char r_msg[1024] = {0};
                //             int len = snprintf(r_msg, sizeof(r_msg), "type "C_BOLD""SV_format""C_RESET" cannot be indexed",
                //                                SV_v_args(list_ty_sv));
                //             const SV msg = SV_from_str_len(r_msg, len);
                //             type_error(&source_info->source, &source_info->source_filename,
                //                        &fn.error_info_list.array[instr.error_info_index].info.index_array.index_expr.sv,
                //                        &msg);
                //             exit(1);
                //         }
                //         case TC_TY_List:
                //         case TC_TY_Slice: {
                //             element_ty = list.type.info.contained;
                //             break;
                //         }
                //         case TC_TY_Array: {
                //             element_ty = list.type.info.t_array.contained;
                //             break;
                //         }
                //     }
                //     // = source_list_ty.info.contained; // get_list_inner_type(source_list_ty);
                //     const SV ele_sv = format_TC_typeid(element_ty, identifier_map, nct);
                //     // 3. Wrap it in a Reference! ('Ref<Char>')
                //     // TC_typeid ref_ty = wrap_in_ref(element_ty);
                //
                //     // const size_t MAX_SIZE = 256;
                //     // char* rs = malloc(sizeof(char) * MAX_SIZE);
                //     // int len = snprintf(rs, MAX_SIZE, "Ref<"SV_format">", SV_v_args(ele_sv));
                //     // rs = realloc(rs, len);
                //     // const SV ref_span = SV_from_str_len(rs, len);
                //
                //     const TC_typeid ref_ty = (TC_typeid){
                //         .ty = TC_TY_Ref,
                //         .info.contained = TC_typeid_new_copy(element_ty), // (TC_typeid *) element_ty,
                //         // .full_typeuse_span = (TokenSpan){.sv = ref_span}
                //     };
                //
                //     // 4. Assign this type to the destination temporal (%13)
                //     temporals_map->array[instr.info.index_ptr.dest.val].type = ref_ty;
                //
                //     // 5. Emit the instruction
                //     // printf("   %%%lu = INDEX_PTR %%%lu [ %%%lu ]\n",
                //     //        instr.info.index_ptr.dest.val,
                //     //        //instr.info.index_ptr.array_ptr.val,
                //     //        list.temporal.val,
                //     //        instr.info.index_ptr.index_val.val);
                //     SizeAlign sz = calc_size_of(element_ty, nct, target_info);
                //     mir_emit(&mir_instrs, (MIR_instr) {
                //         .type = MIR_TY__CALC_PTR,
                //         .info.calc_ptr = {
                //             .dest = instr.info.index_ptr.dest.val,
                //             .base = list.temporal.val,
                //             .offset = 0,
                //             .scale = sz.size,
                //             .index = instr.info.index_ptr.index_val.val,
                //             .has_index = true
                //         }
                //     });
                //
                //
                //     break;
                // }
                case IR_INST_TY__INDEX_PTR: {
                    // 1. Auto-deref the container being indexed (List<T>, Slice<T>, or Array<T, N>)
                    const TC_typeid source_list_ty = temporals_map->array[instr.info.index_ptr.array_ptr.val].type;
                    const IR_temporal_and_TC_type list = perf_auto_deref(
                        &source_list_ty,
                        &instr.info.index_ptr.array_ptr,
                        &highest_temporal,
                        temporals_map,
                        &promote_to_stack_temps,
                        nct,
                        target_info,
                        &mir_instrs
                    );
                    const SV list_ty_sv = format_TC_typeid(&list.type, identifier_map, nct);
                    // 2. Extract the inner element type
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
                    // 3. Coerce index operand to Uint32
                    const TC_typeid index_val_ty = TC_typeid_deep_copy(&temporals_map->array[instr.info.index_ptr.index_val.val].type);
                    const TC_typeid uint32_ty = (TC_typeid){
                        .ty = TC_TY_Primitive,
                        .info.primitive = TC_Primitive__Uint32
                    };
                    const IR_temporal index_temp = perf_autos(
                        &instr.info.index_ptr.index_val,
                        &index_val_ty,
                        &uint32_ty,
                        &highest_temporal,
                        identifier_map,
                        nct,
                        temporals_map,
                        source_info,
                        &fn.error_info_list.array[instr.error_info_index].info.index_array.index_expr.sv,
                        &temporals_map_values,
                        &promote_to_stack_temps,
                        target_info,
                        &mir_instrs
                    );
                    // 4. Wrap element type in Ref<T> and register destination temporal
                    const TC_typeid ref_ty = (TC_typeid){
                        .ty = TC_TY_Ref,
                        .info.contained = TC_typeid_new_copy(element_ty),
                    };
                    temporals_map->array[instr.info.index_ptr.dest.val] = (TC_Entry){
                        .known_temporal = true,
                        .type = ref_ty,
                        .is_value_known = false,
                        .is_addressable = false
                    };
                    // 5. Resolve base data pointer:
                    //    - TC_TY_Array: stack slot address is directly the contiguous element buffer
                    //    - TC_TY_Slice / TC_TY_List: stack slot is header struct { ptr, len [, cap] }; load data ptr at offset 0
                    uint64_t base_ptr_temp = list.temporal.val;
                    if (list.type.ty == TC_TY_Slice || list.type.ty == TC_TY_List) {
                        const uint64_t data_ptr_temp = highest_temporal++;
                        if (temporals_map->cap < data_ptr_temp + 1) {
                            TC_Entry_LIST_inflate(temporals_map, data_ptr_temp + 5);
                        }
                        while (temporals_map->len <= data_ptr_temp) {
                            TC_Entry_LIST_push(temporals_map, (TC_Entry){.known_temporal = false});
                        }
                        temporals_map->array[data_ptr_temp] = (TC_Entry){
                            .known_temporal = true,
                            .type = TC_typeid_deep_copy(&ref_ty),
                            .is_value_known = false,
                            .is_addressable = false
                        };
                        mir_emit(&mir_instrs, (MIR_instr){
                            .type = MIR_TY__READ_PTR,
                            .info.read_ptr = {
                                .dest = data_ptr_temp,
                                .ptr = list.temporal.val,
                                .offset = 0,
                                .read_size = target_info->ptr_size,
                                .value_type = TC_typeid_deep_copy(&ref_ty)
                            }
                        });
                        base_ptr_temp = data_ptr_temp;
                    }
                    // 6. Emit MIR_TY__CALC_PTR scaled by element size
                    const SizeAlign sz = calc_size_of(element_ty, nct, target_info);
                    mir_emit(&mir_instrs, (MIR_instr){
                        .type = MIR_TY__CALC_PTR,
                        .info.calc_ptr = {
                            .dest = instr.info.index_ptr.dest.val,
                            .base = base_ptr_temp,
                            .offset = 0,
                            .scale = sz.size,
                            .index = index_temp.val,
                            .has_index = true
                        }
                    });
                    break;
                }
                case IR_INST_TY__READ_PTR: {
                    // printf("   %%%lu = READ_PTR %%%lu\n", instr.info.read_ptr.dest.val, instr.info.read_ptr.ptr.val);
                    SizeAlign sz = calc_size_of(&temporals_map->array[instr.info.read_ptr.dest.val].type, nct, target_info);
                    mir_emit(&mir_instrs, (MIR_instr) {
                        .type = MIR_TY__READ_PTR,
                        .info.read_ptr = {
                            .dest = instr.info.read_ptr.dest.val,
                            .ptr = instr.info.read_ptr.ptr.val,
                            .offset = 0,
                            .read_size = sz.size,
                            .value_type = TC_typeid_deep_copy(&temporals_map->array[instr.info.read_ptr.dest.val].type)
                        }
                    });
                    break;
                }
                case IR_INST_TY__MAKE_STRUCT: {
                    const TC_typeid struct_type = temporals_map->array[instr.info.make_struct.dest.val].type;
                    if (struct_type.ty != TC_TY_Struct) {
                        INTERNAL_ERROR("MAKE_STRUCT with non struct type")
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
                                    &temporals_map_values,
                                    &promote_to_stack_temps,
                                    target_info,
                                    &mir_instrs
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


                    MIR_struct_init_field_LIST fields = MIR_struct_init_field_LIST_new_with_capacity(sdef.fields.len);
                    for (size_t n = 0; n < sdef.fields.len; n++) {
                        MIR_struct_init_field_LIST_push(&fields, (MIR_struct_init_field){
                            .offset = sdef.layout.field_offsets[n],
                            .temp = args.array[n].val
                        });
                    }

                    mir_emit(&mir_instrs, (MIR_instr) {
                        .type = MIR_TY__STRUCT_INIT,
                        .info.struct_init = {
                            .dest = instr.info.make_struct.dest.val,
                            .struct_type_id = struct_type.info.type_id,
                            .fields = fields,
                            .sa = calc_size_of(&struct_type, nct, target_info)
                        }
                    });

                    // printf("   %%%lu = MAKE_STRUCT (%u size, %u align) { ", instr.info.make_struct.dest.val,
                    //        sdef.layout.size, sdef.layout.align);
                    // for (size_t n = 0; n < sdef.fields.len; n++) {
                    //     printf("%%%lu at %u", args.array[n].val, sdef.layout.field_offsets[n]);
                    //     if (n + 1 != sdef.fields.len) printf(", ");
                    //     else printf(" ");
                    // }
                    // printf("}\n");


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

                    IR_temporal_LIST args = IR_temporal_LIST_new_with_capacity(edef.variants.array[variant_n].values_len);
                    for (size_t n = 0; n < edef.variants.array[variant_n].values_len; n++) {
                        const TC_typeid arg_t = edef.variants.array[variant_n].values[n];
                        const IR_temporal arg_v = instr.info.make_enum.args->array[n];
                        const TokenSpan arg_span = fn.error_info_list.array[instr.error_info_index].info.make_enum.items[n];

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
                            &temporals_map_values,
                            &promote_to_stack_temps,
                            target_info,
                            &mir_instrs
                        );
                        IR_temporal_LIST_push(&args, out);
                    }



                    MIR_temporal_id_LIST mir_args = MIR_temporal_id_LIST_new_with_capacity(args.len);
                    for (size_t n = 0; n < args.len; n++) {
                        MIR_temporal_id_LIST_push(&mir_args, args.array[n].val);
                    }

                    mir_emit(&mir_instrs, (MIR_instr) {
                        .type = MIR_TY__MAKE_ENUM,
                        .info.make_enum = {
                            .dest = instr.info.make_enum.dest.val,
                            .enum_type_id = enum_type.info.type_id,
                            .variant_idx = variant_n,
                            .args = mir_args,
                            .sa = calc_size_of(&enum_type, nct, target_info)
                        }
                    });

                    // printf("   %%%lu = MAKE_ENUM (size %u, align %u) { .variant_val=%lu", instr.info.make_enum.dest.val,
                    //        edef.size, edef.align, variant_n);
                    // if (args.len > 0) printf(", .value=[");
                    // else printf(" ");
                    //
                    // for (size_t n = 0; n < args.len; n++) {
                    //     printf("%%%lu", args.array[n].val);
                    //     if (n + 1 != args.len) printf(", ");
                    // }
                    // if (args.len > 0) printf("] ");
                    // printf("}\n");

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
                            &temporals_map_values,
                            &promote_to_stack_temps,
                            target_info,
                            &mir_instrs
                        );
                        IR_temporal_LIST_push(&items, out);
                    }

                    // const SizeAlign sa = calc_size_of(&base_type, nct, target_info);
                    // const SizeAlign it_sa = calc_size_of(&inner_type, nct, target_info);

                    MIR_temporal_id_LIST mir_items = MIR_temporal_id_LIST_new_with_capacity(items.len);
                    for (size_t n = 0; n < items.len; n++) {
                        MIR_temporal_id_LIST_push(&mir_items, items.array[n].val);
                    }

                    if (base_type.ty != TC_TY_Array && base_type.ty != TC_TY_List) {
                        INTERNAL_ERROR()
                    }

                    mir_emit(&mir_instrs, (MIR_instr) {
                        .type = MIR_TY__MAKE_ARRAY,
                        .info.make_array = {
                            .dest = instr.info.make_array.dest.val,
                            .array_type = TC_typeid_deep_copy(&base_type),
                            .item_type = TC_typeid_deep_copy(&inner_type),
                            .items = mir_items,
                            .is_heap = base_type.ty != TC_TY_Array
                        }
                    });

                    // printf("   %%%lu = MAKE_ARRAY (stack: size %lu, align %lu) ", instr.info.make_array.dest.val,
                    //        sa.size, sa.align);
                    // if (base_type.ty == TC_TY_Array) {
                    //     printf("{STACK ARRAY}");
                    // } else if (base_type.ty == TC_TY_List) {
                    //     printf("{HEAP ARRAY}");
                    //     // instr.info.make_array.items->len;
                    //
                    //     printf(" {%lu bytes on heap, %lu align}", it_sa.size * instr.info.make_array.items->len,
                    //            it_sa.align);
                    // } else {
                    //     INTERNAL_ERROR("unknown list type");
                    // }
                    // printf(" [ ");
                    // for (size_t n = 0; n < items.len; n++) {
                    //     printf("%%%lu", items.array[n].val);
                    //     if (n + 1 != items.len) printf(", ");
                    //     else printf(" ");
                    // }
                    // printf("]\n");

                    break;
                }
                case IR_INST_TY__BRANCH: {
                    const TC_typeid t = temporals_map->array[instr.info.branch.cond.val].type;

                    const IR_temporal out = perf_bool(
                        &instr.info.branch.cond,
                        &t,
                        &highest_temporal,
                        temporals_map,
                        &promote_to_stack_temps,
                        nct,
                        target_info,
                        &mir_instrs
                    );


                    // printf("   BRANCH <Bool> %%%lu, Block %lu, Block %lu\n", out.val, instr.info.branch.branch_true, instr.info.branch.branch_false);
                    mir_emit(&mir_instrs, (MIR_instr) {
                        .type = MIR_TY__BRANCH,
                        .info.branch = {
                            .cond = out.val,
                            .branch_true = instr.info.branch.branch_true,
                            .branch_false = instr.info.branch.branch_false
                        }
                    });
                    break;
                }
                case IR_INST_TY__JUMP: {
                    mir_emit(&mir_instrs, (MIR_instr) {
                        .type = MIR_TY__JUMP,
                        .info.jump = {
                            .target_block = instr.info.jump.block
                        }
                    });
                    // printf("   JUMP Block %lu\n", instr.info.jump.block);
                    break;
                }
                case IR_INST_TY__TO_BOOL: {
                    const TC_typeid src_t = temporals_map->array[instr.info.to_bool.src.val].type;
                    const IR_temporal_and_TC_type new_src = perf_auto_deref(
                        &src_t,
                        &instr.info.to_bool.src,
                        &highest_temporal,
                        temporals_map,
                        &promote_to_stack_temps,
                        nct,
                        target_info,
                        &mir_instrs
                    );
                    if (new_src.type.ty == TC_TY_Primitive && new_src.type.info.primitive == TC_Primitive__Bool) {
                        // rewrite map new_stc -> dest
                        rewrite_map.array[instr.info.to_bool.dest.val].has_rewrite = true;
                        rewrite_map.array[instr.info.to_bool.dest.val].rewrite = new_src.temporal;
                    } else if (new_src.type.ty == TC_TY_Primitive) {

                        const size_t c_zero = highest_temporal++;
                        mir_emit(&mir_instrs, (MIR_instr){
                            .type = MIR_TY__CONST_INT,
                            .info.const_int = {
                                .dest = c_zero,
                                .val = 0,
                                .type = TC_typeid_deep_copy(&new_src.type)
                            }
                        });
                        mir_emit(&mir_instrs, (MIR_instr) {
                            .type = MIR_TY__OP_CMP,
                            .info.cmp = {
                                .dest = instr.info.to_bool.dest.val,
                                .a = new_src.temporal.val,
                                .b = c_zero,
                                .type = TC_typeid_deep_copy(&new_src.type),
                                .cmp_kind = MIR_CMP_NOT_EQ
                            }
                        });
                        // printf("   %%%lu = CMP_NOT_EQ %%%lu, 0\n", instr.info.to_bool.dest.val, new_src.temporal.val);
                    } else {
                        // type error
                        char r_msg[1024] = {0};
                        const SV got_t_sv = format_TC_typeid(&new_src.type, identifier_map, nct);
                        int len = snprintf(r_msg, 1024, "needs to be a "C_BOLD"Bool"C_RESET" value, got "C_BOLD""SV_format""C_RESET"", SV_v_args(got_t_sv));
                        const SV msg = SV_from_str_len(r_msg, len);
                        type_error(&source_info->source, &source_info->source_filename, &instr.info.to_bool.expr.sv, &msg);
                        exit(1);
                    }
                    break;
                }
                case IR_INST_TY__RETURN_VOID: {
                    mir_emit(&mir_instrs, (MIR_instr) {
                        .type = MIR_TY__RETURN_VOID,
                    });
                    // printf("   RETURN\n");
                    break;
                }
                case IR_INST_TY__RETURN: {
                    if (fn.fn_ret_t.tt_ty == IR_TY_Void) {
                        const SV msg = SV_from_str_len("expected no value on return based on function signature", 55);
                        type_error(&source_info->source, &source_info->source_filename,
                                   &fn.error_info_list.array[instr.error_info_index].info.return_value.expr.sv, &msg);
                        exit(1);
                    }
                    const TC_typeid t = resolve_tc_type(&fn.fn_ret_t, nct, target_info, source_info, identifier_map);
                    const TC_typeid intake_t = temporals_map->array[instr.info.return_value.val].type;
                    const IR_temporal out = perf_autos(
                        &instr.info.return_value,
                        &intake_t,
                        &t,
                        &highest_temporal,
                        identifier_map,
                        nct,
                        temporals_map,
                        source_info,
                        &fn.error_info_list.array[instr.error_info_index].info.return_value.expr.sv, &temporals_map_values,
                        &promote_to_stack_temps,
                        target_info,
                        &mir_instrs
                    );
                    // const SV out_t_sv = format_TC_typeid(&t, identifier_map, nct);
                    // printf("   RETURN <"SV_format"> %%%lu\n", SV_v_args(out_t_sv), out.val);
                    mir_emit(&mir_instrs, (MIR_instr) {
                        .type = MIR_TY__RETURN,
                        .info.return_val = {
                            .val = out.val,
                            .type = TC_typeid_deep_copy(&t)
                        }
                    });
                    break;
                }
                case IR_INST_TY__GET_STRUCT_FIELD_PTR: {
                    // instr.info.get_struct_field_ptr.struct_ptr
                    const TC_typeid struct_ptr_t = temporals_map->array[instr.info.get_struct_field_ptr.struct_ptr.val].
                            type;
                    // const SV struct_ptr_t_sv = format_TC_typeid(&struct_ptr_t, identifier_map, nct);
                    if (struct_ptr_t.ty != TC_TY_Ref && !temporals_map->array[instr.info.get_struct_field_ptr.struct_ptr.val].is_addressable) {
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
                            if (!is_type_eq(&field_t, &dest_t)) {
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


                            // printf("   %%%lu = CONST_INT <Uint32> (%u)\n", instr.info.get_struct_field_ptr.dest.val, struct_t.info.t_array.array_size);
                            mir_emit(&mir_instrs, (MIR_instr) {
                                .type = MIR_TY__CONST_INT,
                                .info.const_int = {
                                    .dest = instr.info.get_struct_field_ptr.dest.val,
                                    .val = struct_t.info.t_array.array_size,
                                    .type = (TC_typeid) {
                                        .ty = TC_TY_Primitive,
                                        .info.primitive = TC_Primitive__Uint32
                                    }
                                }
                            });

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
                            const TC_typeid field_t = (TC_typeid) {
                                .ty = TC_TY_Ref,
                                .info.contained = TC_typeid_new_copy(&sdef.fields.array[n].type)
                            };
                            const SizeAlign field_sz = calc_size_of(&sdef.fields.array[n].type, nct, target_info);


                            if (!is_type_eq(&field_t, &dest_t)) {
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

                            // %n = GET_STRUCT_PTR offset x bytes, read size x bytes, read type <>, ptr %x, ptr type <>
                            // printf("   %%%lu = GET_STRUCT_FIELD_PTR offset %u bytes, read size %lu bytes, read align %lu bytes, read type "SV_format", ptr %%%lu, ptr type "SV_format"\n",
                            //     instr.info.get_struct_field_ptr.dest.val,
                            //     sdef.layout.field_offsets[n],
                            //     field_sz.size,
                            //     field_sz.align,
                            //     SV_v_args(field_t_sv),
                            //     instr.info.get_struct_field_ptr.struct_ptr.val,
                            //     SV_v_args(struct_ptr_t_sv)
                            // );
                            mir_emit(&mir_instrs, (MIR_instr) {
                                .type = MIR_TY__CALC_PTR,
                                .info.calc_ptr = {
                                    .dest = instr.info.get_struct_field_ptr.dest.val,
                                    .base = instr.info.get_struct_field_ptr.struct_ptr.val,
                                    .offset = sdef.layout.field_offsets[n],
                                    .scale = field_sz.size,
                                    .index = 0,
                                    .has_index = false,
                                }
                            });

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


                    OverloadResolutionResult over_rs = resolve_function_overload(
                        fn_name_id,
                        raw_args,
                        instr.ty == IR_INST_TY__FN_CALL_RETVAL ? instr.info.fn_call_retval.dest : (IR_temporal){0},
                        temporals_map,
                        functions,
                        nct,
                        target_info,
                        source_info,
                        identifier_map,
                        &temporals_map_values
                    );

                    // printf("[LOWERING] resolve_function_overload .status=%u, .is_success=%u\n", over_rs.status, over_rs.status == OVERLOAD_RESULT_SUCCESS);

                    if (over_rs.status != OVERLOAD_RESULT_SUCCESS) {
                        const TokenSpan fn_name_span = is_retval
                        ? instr.info.fn_call_retval.name.span
                        : instr.info.fn_call_retvoid.name.span;

                        size_t MAX_SIZE = 1024;
                        char* r_msg = malloc(sizeof(char) * MAX_SIZE);
                        int len = 0;

                        if (over_rs.status == OVERLOAD_RESULT_ARITY_MISMATCH) {
                            len = snprintf(r_msg, MAX_SIZE, "no overload of function '"SV_format"' takes %lu arguments",
                                           SV_v_args(fn_name_span.sv), raw_args->len);
                        } else if (over_rs.status == OVERLOAD_RESULT_AMBIGUOUS) {
                            len = snprintf(r_msg, MAX_SIZE, "ambiguous call to function '"SV_format"'", SV_v_args(fn_name_span.sv));
                        } else {
                            len = snprintf(r_msg, MAX_SIZE, "no matching overload for function '"SV_format"'", SV_v_args(fn_name_span.sv));
                        }
                        const SV msg = SV_from_str_len(r_msg, len);
                        type_error(&source_info->source, &source_info->source_filename, &fn_name_span.sv, &msg);
                        free(r_msg);
                        exit(1);
                    }
                    const IR_func* target_fn = over_rs.resolved_fn;

                    // const SV raw_fn_name = target_fn->raw_name;

                    // 2. Coerce arguments using perf_autos
                    IR_temporal_LIST coerced_args = IR_temporal_LIST_new_with_capacity(raw_args->len);
                    TC_typeid_LIST coerced_args_types = TC_typeid_LIST_new_with_capacity(raw_args->len);

                    for (size_t k = 0; k < raw_args->len; k++) {
                        const IR_temporal arg_temp = raw_args->array[k];
                        const TC_typeid arg_t = temporals_map->array[arg_temp.val].type;
                        const TC_typeid expected_t = resolve_tc_type(&target_fn->args.array[k].type, nct,
                                                                     target_info, source_info, identifier_map);

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
                            &temporals_map_values,
                            &promote_to_stack_temps,
                            target_info,
                            &mir_instrs
                        );
                        IR_temporal_LIST_push(&coerced_args, coerced);
                        TC_typeid_LIST_push(&coerced_args_types, TC_typeid_deep_copy(&expected_t));
                    }

                    bool emit_drop_for_ret = false;
                    // 3. Handle return value for RETVAL
                    if (is_retval) {
                        const TC_typeid expected_ret_t = temporals_map->array[instr.info.fn_call_retval.dest.val].type;
                        const TC_typeid fn_ret_t = resolve_tc_type(&target_fn->fn_ret_t, nct, target_info, source_info, identifier_map);


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
                            .type = TC_typeid_deep_copy(&fn_ret_t),
                            .is_value_known = false,
                            .is_addressable = false
                        };

                        // const TC_typeid expected_ret_t_2 = temporals_map->array[instr.info.fn_call_retval.dest.val].type;


                        // const SV expected_ret_t_sv = format_TC_typeid(&fn_ret_t, identifier_map, nct);
                        // printf("   %%%lu <"SV_format"> = ", instr.info.fn_call_retval.dest.val, SV_v_args(expected_ret_t_sv));

                        MIR_temporal_id_LIST args = MIR_temporal_id_LIST_new_with_capacity(coerced_args.len);
                        for (size_t n = 0; n < coerced_args.len; n++) {
                            MIR_temporal_id_LIST_push(&args, coerced_args.array[n].val);
                        }
                        mir_emit(&mir_instrs, (MIR_instr){
                            .type = MIR_TY__CALL,
                            .info.call = {
                                .dest = instr.info.fn_call_retval.dest.val,
                                .has_dest = true,
                                .fn_name = target_fn->fn_name,
                                .raw_name = target_fn->raw_name,
                                .args = args,
                                .arg_types = coerced_args_types,
                                .ret_type = TC_typeid_deep_copy(&fn_ret_t)
                            }
                        });
                    } else {
                        MIR_temporal_id_LIST args = MIR_temporal_id_LIST_new_with_capacity(coerced_args.len);
                        for (size_t n = 0; n < coerced_args.len; n++) {
                            MIR_temporal_id_LIST_push(&args, coerced_args.array[n].val);
                        }
                        mir_emit(&mir_instrs, (MIR_instr){
                            .type = MIR_TY__CALL,
                            .info.call = {
                                .dest = 0,
                                .has_dest = false,
                                .fn_name = target_fn->fn_name,
                                .raw_name = target_fn->raw_name,
                                .args = args,
                                .arg_types = coerced_args_types,
                                .ret_type = (TC_typeid){.ty = TC_TY_Void}
                            }
                        });
                        // printf("   ");
                    }

                    // 4. Print / Emit MIR instruction
                    // ... print function name, destination, and coerced_args ...

                    // printf("FUNC_CALL '"SV_format"' {raw='"SV_format"'} [", SV_v_args(target_fn->fn_name.span.sv),
                    //        SV_v_args(raw_fn_name));
                    // for (size_t n = 0; n < coerced_args.len; n++) {
                    //     const SV arg_t_sv = format_TC_typeid(&coerced_args_types.array[n], identifier_map, nct);
                    //     printf("%%%lu <"SV_format">", coerced_args.array[n].val, SV_v_args(arg_t_sv));
                    //     if (n + 1 != coerced_args.len) {
                    //         printf(", ");
                    //     }
                    // }
                    // printf("]\n");

                    if (emit_drop_for_ret) {
                        const TC_typeid ret_t = temporals_map->array[instr.info.fn_call_retval.dest.val].type;
                        // const SV ret_t_sv = format_TC_typeid(&ret_t, identifier_map, nct);
                        // printf("   DROP <"SV_format"> %%%lu [STACK]\n", SV_v_args(ret_t_sv), instr.info.fn_call_retval.dest.val);
                        mir_emit(&mir_instrs, (MIR_instr){
                            .type = MIR_TY__DROP,
                            .info.drop = {
                                .dest = instr.info.fn_call_retval.dest.val,
                                .type = TC_typeid_deep_copy(&ret_t),
                                .drop_inside = false
                            }
                        });
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


                    mir_emit(&mir_instrs, (MIR_instr) {
                        .type = MIR_TY__CALC_PTR,
                        .info.calc_ptr = {
                            .dest = instr.info.enum_get_field_ptr.dest.val,
                            .base = instr.info.enum_get_field_ptr.enum_src.val,
                            .offset = val_offset,
                            .scale = sz.size,
                            .index = 0,
                            .has_index = false
                        }
                    });
                    // printf("   %%%lu = CALC_PTR (%%%lu + %lu) {read size = %lu bytes}\n",
                    //     instr.info.enum_get_field_ptr.dest.val,
                    //     instr.info.enum_get_field_ptr.enum_src.val,
                    //     val_offset,
                    //     sz.size
                    // );

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

                    // printf("   %%%lu = READ_PTR from (%%%lu + %u) {read size = %u bytes}\n",
                    //     instr.info.enum_variant_number_extract.dest.val,
                    //     instr.info.enum_variant_number_extract.enum_src.val,
                    //     0u,
                    //     edef.discriminant_size
                    // );
                    TC_Primitive_TY primitive;
                    switch (edef.discriminant_size) {
                        case 1: {
                            primitive = TC_Primitive__Uint8;
                            break;
                        }
                        case 2: {
                            primitive = TC_Primitive__Uint16;
                            break;
                        }
                        case 4: {
                            primitive = TC_Primitive__Uint32;
                            break;
                        }
                        case 8: {
                            primitive = TC_Primitive__Uint64;
                            break;
                        }
                        default: INTERNAL_ERROR()
                    }

                    mir_emit(&mir_instrs, (MIR_instr) {
                        .type = MIR_TY__READ_PTR,
                        .info.read_ptr = {
                            .dest = instr.info.enum_variant_number_extract.dest.val,
                            .ptr = instr.info.enum_variant_number_extract.enum_src.val,
                            .offset = 0,
                            .read_size = edef.discriminant_size,
                            .value_type = (TC_typeid) {
                                .ty = TC_TY_Primitive,
                                .info.primitive = primitive
                            }
                        }
                    });

                    break;
                }
                case IR_INST_TY__ENUM_VARIANT_NUMBER_FROM_TYPE: {
                    INTERNAL_ERROR("should have been replaced by CONST_INT at type-inference pass")
                    break;
                }
                case IR_INST_TY__MAKE_BOX: {
                    if (!temporals_map->array[instr.info.make_box.dest.val].known_temporal) {
                        INTERNAL_ERROR("MAKE_BOX dest temporal unknown");
                    }
                    const TC_typeid dest_t = temporals_map->array[instr.info.make_box.dest.val].type;
                    if (dest_t.ty != TC_TY_Box) {
                        INTERNAL_ERROR("MAKE_BOX dest is not Box");
                    }
                    if (!temporals_map->array[instr.info.make_box.src.val].known_temporal) {
                        INTERNAL_ERROR("MAKE_BOX src temporal unknown");
                    }
                    const TC_typeid src_t = temporals_map->array[instr.info.make_box.src.val].type;
                    const TC_typeid inner_t = *dest_t.info.contained;
                    const TokenSpan arg_span = fn.error_info_list.array[instr.error_info_index].info.make_box.expr;
                    const IR_temporal out = perf_autos(
                        &instr.info.make_box.src,
                        &src_t,
                        &inner_t,
                        &highest_temporal,
                        identifier_map,
                        nct,
                        temporals_map,
                        source_info,
                        &arg_span.sv,
                        &temporals_map_values,
                        &promote_to_stack_temps,
                        target_info,
                        &mir_instrs
                    );

                    const SizeAlign sz = calc_size_of(&inner_t, nct, target_info);

                    // printf("   %%%lu = MAKE_BOX %%%lu {%lu bytes on heap, %lu align}\n",
                    //        instr.info.make_box.dest.val,
                    //        out.val,
                    //        sz.size,
                    //        sz.align);
                    // mir_emit(&mir_instrs, (MIR_instr) {
                    //     .type = MIR_TY__MAKE_BOX,
                    //     .info.make_box = {
                    //         .dest = instr.info.make_box.dest.val,
                    //         .src = out.val,
                    //         .inner_sa = sz
                    //     }
                    // });
                    UNIMPLEMENTED("use ALLOC_HEAP+STORE")

                    break;
                }
            }
        }

        const MIR_block mir_block = (MIR_block){
            .instructions = mir_instrs
        };
        MIR_block_LIST_push(&mir_blocks, mir_block);
    }

    TC_typeid_LIST fn_args = TC_typeid_LIST_new_with_capacity(fn.args.len);
    for (size_t n = 0; n < fn.args.len; n++) {
        TC_typeid_LIST_push(&fn_args, resolve_tc_type(
            &fn.args.array[n].type,
            nct,
            target_info,
            source_info,
            identifier_map
        ));
    }

    return (MIR_func){
        .fn_name = fn.fn_name,
        .fn_raw_name = fn.raw_name,
        .args = fn_args,
        .blocks = mir_blocks,
        .highest_temporal_number = highest_temporal,
        .temporals = mir_temporals,
        .has_ret = fn.fn_ret_t.tt_ty != IR_TY_Void,
        .ret_type = resolve_tc_type(&fn.fn_ret_t, nct, target_info, source_info, identifier_map)
    };
}

