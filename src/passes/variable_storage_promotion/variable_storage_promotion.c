
#include "variable_storage_promotion.h"

#include "passes/type_utils.h"
#include "passes/typechecking/perf.h"


void variable_storage_promotion_pass(
    const IR_func fn,
    khash_t(sv_u64_map)* restrict identifier_map,
    const TC_Entry_LIST temporals,
    IR_temporal_LIST* restrict promote_to_stack_temps,
    const TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info
) {

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
                    IR_temp_map_LIST_push(&temp_map, (IR_temp_map){
                        instr.info.assign.old,
                        instr.info.assign.dest
                    });
                    break;
                }
                case IR_INST_TY__MATCH_META__TARGET: {
                    IR_temporal_LIST_push(promote_to_stack_temps, instr.info.match_meta__target.target);
                    break;
                }
                case IR_INST_TY__PHI: {
                    for(size_t j = 0; j < instr.info.phi.args->len; j++) {
                        IR_temp_map_LIST_push(&temp_map, (IR_temp_map){
                            instr.info.phi.dest,
                            instr.info.phi.args->array[j].temp
                        });
                        IR_temp_map_LIST_push(&temp_map, (IR_temp_map){
                            instr.info.phi.args->array[j].temp,
                            instr.info.phi.dest
                        });
                        for(size_t k = 0; k < instr.info.phi.args->len; k++) {
                            if(k == j) break;
                            IR_temp_map_LIST_push(&temp_map, (IR_temp_map){
                                instr.info.phi.args->array[j].temp,
                                instr.info.phi.args->array[k].temp
                            });
                            IR_temp_map_LIST_push(&temp_map, (IR_temp_map){
                                instr.info.phi.args->array[k].temp,
                                instr.info.phi.args->array[j].temp
                            });
                        }
                    }
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
                case IR_INST_TY__DROP:
                case IR_INST_TY__EXPLICIT_CAST:
                case IR_INST_TY__CONST_INT:
                case IR_INST_TY__CONST_FLOAT:
                case IR_INST_TY__CONST_STRING:
                case IR_INST_TY__MAKE_ARRAY:
                    break;
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
                case IR_INST_TY__MAKE_BOX:
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
    IR_temp_map_LIST_free(&temp_map);

//     UNIMPLEMENTED("hey")
}
