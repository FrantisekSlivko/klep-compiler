
#include "typeinference.h"
#include "passes/inference_check_types.h"
#include "typeresolve/type_resolve.h"
#include "passes/type_utils.h"
#include "error.h"
#include "overloading/resolve.h"
#include "utils/color.h"
#include "passes/parser/parser.h"

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
                                   // .full_typeuse_span = (TokenSpan){.sv = sv_uint32}
                               }
                           });

        // { ptr; len; cap; }
        // only len is exposed
        uint32_t* list_field_offsets = malloc(sizeof(uint32_t));
        *list_field_offsets = target_info->ptr_size;

        compiler_builtin__list_tc_structdef = (TC_structdef){
            (SpannedIdentifier){
                .identifier = SV_to_identifier(identifier_map, identifier_map_cnt, &list_name),
                .span = (TokenSpan){.sv = list_name},
            },
            list_fields,
            (TC_struct_layout){
                .size = target_info->ptr_size * 3,
                .align = target_info->ptr_size,
                .field_offsets = list_field_offsets
            },
            false,
            0
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
                                   // .full_typeuse_span = (TokenSpan){.sv = sv_uint32}
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
    const TC_typeid ret_t = resolve_tc_type(&ir_ret_t, nct, target_info, source_info, identifier_map);


    TC_arg_LIST args = TC_arg_LIST_new_with_capacity(fn.args.len);
    args.len = fn.args.len;
    for (size_t n = 0; n < fn.args.len; n++) {
        const TC_typeid type = resolve_tc_type(&fn.args.array[n].type, nct, target_info, source_info, identifier_map);
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

                    if(!temporals_map->array[instr.info.assign.old.val].known_temporal) {
                        temporals_map->array[instr.info.assign.old.val] = (TC_Entry){
                            .known_temporal = true,
                            .type = dest_t,
                            .is_addressable = false,
                            .is_value_known = false,
                        };
                    }

                    const TC_typeid new_t = dest_t.ty == TC_TY_Ref ? *dest_t.info.contained : dest_t;

                    if(!temporals_map->array[instr.info.assign.new.val].known_temporal) {
                        temporals_map->array[instr.info.assign.new.val] = (TC_Entry){
                            .known_temporal = true,
                            .type = new_t,
                            .is_addressable = false,
                            .is_value_known = false,
                        };
                    }
                    break;
                }
                case IR_INST_TY__CONST_INT: {
                    // set as INT32 if unknown temporal and make the pass repeat
                    if (!temporals_map->array[instr.info.const_int.dest.val].known_temporal) {
                        temporals_map->array[instr.info.const_int.dest.val] = (TC_Entry){
                            .known_temporal = true,
                            .type = (TC_typeid){
                                .ty = TC_TY_Primitive,
                                .info.primitive = TC_Primitive__Int32,
                                // .full_typeuse_span = (TokenSpan){.sv = SV_from_str_len("Int32", 5)}
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
                                // .full_typeuse_span = (TokenSpan){.sv = SV_from_str_len("Float32", 7)}
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
                            // .full_typeuse_span = (TokenSpan){.sv = SV_from_str_len("Char", 4)}
                        };
                        temporals_map->array[instr.info.const_int.dest.val] = (TC_Entry){
                            .known_temporal = true,
                            .type = (TC_typeid){
                                .ty = TC_TY_Slice,
                                .info.contained = inner,
                                // .full_typeuse_span = (TokenSpan){.sv = SV_from_str_len("Slice<Char>", 11)}
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
                    const TC_typeid t = resolve_tc_type(instr.info.var_decl.type, nct, target_info, source_info, identifier_map);
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

                    const TC_typeid type = resolve_tc_type(instr.info.drop.type, nct, target_info, source_info, identifier_map);

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

                    // when dest is REF, it is unknow if src is T or Ref<T> or Ref<Ref<T>>
                    if (dest_type.ty == TC_TY_Ref) break;

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

                    // const size_t MAX_SIZE = 128;
                    // char* new_type_name = malloc(sizeof(char) * MAX_SIZE);
                    // const int out_len = snprintf(new_type_name, MAX_SIZE, "Ref<"SV_format">",
                                                 // SV_v_args(src_t.full_typeuse_span.sv));
                    // new_type_name = realloc(new_type_name, sizeof(char) * out_len);
                    // const SV new_type = SV_from_str_len(new_type_name, out_len);

                    temporals_map->array[instr.info.ref_of.dest.val] = (TC_Entry){
                        .known_temporal = true,
                        .type = (TC_typeid){
                            .ty = TC_TY_Ref,
                            .info.contained = out,
                            // .full_typeuse_span = (TokenSpan){.sv = new_type},
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
                                // .full_typeuse_span = (TokenSpan){.sv = SV_from_str_len("Bool", 4)},
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
                                // .full_typeuse_span = (TokenSpan){.sv = SV_from_str_len("Bool", 4)},
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
                                // .full_typeuse_span = (TokenSpan){.sv = SV_from_str_len("Bool", 4)},
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
                                // .full_typeuse_span = (TokenSpan){.sv = SV_from_str_len("Bool", 4)},
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
                        if (instr.info.make_array.items->len == 0) break;
                        bool all_known = true;
                        for (size_t n = 0; n < instr.info.make_array.items->len; n++) {
                            if (!temporals_map->array[instr.info.make_array.items->array[n].val].known_temporal) { all_known = false; break; }
                        }
                        if (!all_known) break;

                        const TC_typeid first = temporals_map->array[instr.info.make_array.items->array[0].val].type;
                        for (size_t n = 1; n < instr.info.make_array.items->len; n++) {
                            if (!is_type_eq(&first, &temporals_map->array[instr.info.make_array.items->array[n].val].type)) {
                                INTERNAL_ERROR("MAKE_ARRAY: item types differ");
                            }
                        }

                        TC_typeid* inner = malloc(sizeof(TC_typeid));
                        *inner = first;

                        // const SV inner_sv = format_TC_typeid(inner, identifier_map, nct);
                        // const size_t MAX_SIZE = 256;
                        // char* name = malloc(MAX_SIZE);
                        // const int len = snprintf(name, MAX_SIZE, "Array<"SV_format", %lu>",
                                                 // SV_v_args(inner_sv), instr.info.make_array.items->len);

                        temporals_map->array[instr.info.make_array.dest.val] = (TC_Entry){
                            .known_temporal = true,
                            .type = (TC_typeid){
                                .ty = TC_TY_Array,
                                .info.t_array = {inner, (uint32_t) instr.info.make_array.items->len},
                                // .full_typeuse_span = (TokenSpan){.sv = SV_from_str_len(name, len)}
                            },
                            .is_value_known = false,
                            .is_addressable = false
                        };
                        block_id = (ptrdiff_t) fn.blocks.len;
                        perf_jump = true;
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
                                // .full_typeuse_span = (TokenSpan){.sv = SV_from_str_len("Bool", 4)},
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

                                    // const size_t MAX_SIZE = 128;
                                    // char* new_type_name = malloc(sizeof(char) * MAX_SIZE);
                                    // const int out_len = snprintf(new_type_name, MAX_SIZE, "Ref<"SV_format">",
                                                                 // SV_v_args(ref_inner->full_typeuse_span.sv));
                                    // new_type_name = realloc(new_type_name, sizeof(char) * out_len);
                                    // const SV new_type = SV_from_str_len(new_type_name, out_len);


                                    temporals_map->array[instr.info.get_struct_field_ptr.dest.val] = (TC_Entry){
                                        .known_temporal = true,
                                        .type = (TC_typeid){
                                            .ty = TC_TY_Ref,
                                            .info.contained = ref_inner,
                                            // .full_typeuse_span = (TokenSpan){.sv = new_type},
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

                                // const size_t MAX_SIZE = 128;
                                // char* new_type_name = malloc(sizeof(char) * MAX_SIZE);
                                // const int out_len = snprintf(new_type_name, MAX_SIZE, "Ref<"SV_format">",
                                                             // SV_v_args(ref_inner->full_typeuse_span.sv));
                                // new_type_name = realloc(new_type_name, sizeof(char) * out_len);
                                // const SV new_type = SV_from_str_len(new_type_name, out_len);


                                temporals_map->array[instr.info.get_struct_field_ptr.dest.val] = (TC_Entry){
                                    .known_temporal = true,
                                    .type = (TC_typeid){
                                        .ty = TC_TY_Ref,
                                        .info.contained = ref_inner,
                                        // .full_typeuse_span = (TokenSpan){.sv = new_type},
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

                                // const size_t MAX_SIZE = 128;
                                // char* new_type_name = malloc(sizeof(char) * MAX_SIZE);
                                // const int out_len = snprintf(new_type_name, MAX_SIZE, "Ref<"SV_format">",
                                                             // SV_v_args(ref_inner->full_typeuse_span.sv));
                                // new_type_name = realloc(new_type_name, sizeof(char) * out_len);
                                // const SV new_type = SV_from_str_len(new_type_name, out_len);


                                temporals_map->array[instr.info.get_struct_field_ptr.dest.val] = (TC_Entry){
                                    .known_temporal = true,
                                    .type = (TC_typeid){
                                        .ty = TC_TY_Ref,
                                        .info.contained = ref_inner,
                                        // .full_typeuse_span = (TokenSpan){.sv = new_type},
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

                        // const size_t MAX_SIZE = 128;
                        // char* new_type_name = malloc(sizeof(char) * MAX_SIZE);
                        // const int out_len = snprintf(new_type_name, MAX_SIZE, "Ref<"SV_format">",
                                                     // SV_v_args(dest.full_typeuse_span.sv));
                        // new_type_name = realloc(new_type_name, sizeof(char) * out_len);
                        // const SV new_type = SV_from_str_len(new_type_name, out_len);

                        temporals_map->array[instr.info.read_ptr.ptr.val] = (TC_Entry){
                            .known_temporal = true,
                            .type = (TC_typeid){
                                .ty = TC_TY_Ref,
                                .info.contained = out,
                                // .full_typeuse_span = (TokenSpan){.sv = new_type}, //ref_inner->full_typeuse_span
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
                        type = resolve_tc_type(instr.info.make_struct.type, nct, target_info, source_info, identifier_map);
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


                    OverloadResolutionResult over_rs = resolve_function_overload(
                        name,
                        lk,
                        instr.ty == IR_INST_TY__FN_CALL_RETVAL ? instr.info.fn_call_retval.dest : (IR_temporal){0},
                        temporals_map,
                        functions,
                        nct,
                        target_info,
                        source_info,
                        identifier_map,
                        NULL
                    );

                    // printf("[INFERENCE] resolve_function_overload .status=%u, .is_success=%u\n", over_rs.status, over_rs.status == OVERLOAD_RESULT_SUCCESS);

                    if (over_rs.status == OVERLOAD_RESULT_SUCCESS) {
                        const IR_func* func = over_rs.resolved_fn;

                        if (instr.ty == IR_INST_TY__FN_CALL_RETVAL) {
                            const TC_typeid t = resolve_tc_type(&func->fn_ret_t, nct, target_info, source_info, identifier_map);
                            temporals_map->array[instr.info.fn_call_retval.dest.val] = (TC_Entry){
                                .known_temporal = true,
                                .type = t,
                                .is_value_known = false,
                                .is_addressable = false
                            };
                        }

                        if (func->args.len != lk->len) INTERNAL_ERROR()

                        for (size_t n = 0; n < func->args.len; n++) {
                            if (!temporals_map->array[lk->array[n].val].known_temporal) {

                                const TC_typeid type = resolve_tc_type(
                                    &over_rs.resolved_fn->args.array[n].type,
                                    nct,
                                    target_info,
                                    source_info,
                                    identifier_map
                                );

                                temporals_map->array[lk->array[n].val] = (TC_Entry){
                                .known_temporal = true,
                                .type = type,
                                .is_value_known = false,
                                .is_addressable = false
                                };
                            }
                        }
                    }

                    // for (size_t n = 0; n < functions->len; n++) {
                    //     if (functions->array[n].fn_name.identifier == name) {
                    //         if (instr.ty == IR_INST_TY__FN_CALL_RETVAL) {
                    //             const TC_typeid t = resolve_tc_type(&functions->array[n].fn_ret_t, nct, target_info,
                    //                                                 source_info, identifier_map);
                    //             temporals_map->array[instr.info.fn_call_retval.dest.val] = (TC_Entry){
                    //                 .known_temporal = true,
                    //                 .type = t,
                    //                 .is_value_known = false,
                    //                 .is_addressable = false
                    //             };
                    //         }
                    //         if (lk->len == functions->array[n].args.len) {
                    //             for (size_t k = 0; k < lk->len; k++) {
                    //                 if (!temporals_map->array[lk->array[k].val].known_temporal) {
                    //                     const TC_typeid type = resolve_tc_type(
                    //                         &functions->array[n].args.array[k].type, nct, target_info, source_info, identifier_map);
                    //                     temporals_map->array[lk->array[k].val] = (TC_Entry){
                    //                         .known_temporal = true,
                    //                         .type = type,
                    //                         .is_value_known = false,
                    //                         .is_addressable = false
                    //                     };
                    //                 }
                    //             }
                    //         }
                    //     }
                    // }

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
                            // const size_t MAX_SIZE = 256;
                            // char* t_sv = malloc(sizeof(char) * MAX_SIZE);
                            // const SV inner_t_sv = format_TC_typeid(inner_type, identifier_map, nct);
                            // const int t_len = snprintf(t_sv, MAX_SIZE, "Ref<"SV_format">", SV_v_args(inner_t_sv));

                            temporals_map->array[instr.info.index_ptr.dest.val] = (TC_Entry){
                                .known_temporal = true,
                                .type = (TC_typeid){
                                    .ty = TC_TY_Ref,
                                    .info.contained = inner_type,
                                    // .full_typeuse_span = (TokenSpan){.sv = (SV){.start = t_sv, .len = t_len}}
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
                                // .full_typeuse_span = (TokenSpan){.sv = SV_from_str_len("Uint32", 6)}
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
                    const TC_typeid t = resolve_tc_type(instr.info.explicit_cast.type, nct, target_info, source_info, identifier_map);
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
                        type = resolve_tc_type(instr.info.make_enum.type, nct, target_info, source_info, identifier_map);
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
                                // .full_typeuse_span = (TokenSpan){.sv = SV_from_str_len("Uint32", 6)}
                            },
                            .is_value_known = false,
                            .is_addressable = false,
                        };
                        break;
                    }

                    TC_Primitive_TY prim;
                    // SV prim_sv;
                    switch (nct->enums.array[enum_t.info.type_id].discriminant_size) {
                        case 1: {
                            prim = TC_Primitive__Uint8;
                            // prim_sv = SV_from_str_len("Uint8", 5);
                            break;
                        }
                        case 2: {
                            prim = TC_Primitive__Uint16;
                            // prim_sv = SV_from_str_len("Uint16", 6);
                            break;
                        }
                        case 4: {
                            prim = TC_Primitive__Uint32;
                            // prim_sv = SV_from_str_len("Uint32", 6);
                            break;
                        }
                        case 8: {
                            prim = TC_Primitive__Uint64;
                            // prim_sv = SV_from_str_len("Uint64", 6);
                            break;
                        }
                        default: INTERNAL_ERROR()
                    }

                    temporals_map->array[instr.info.enum_variant_number_extract.dest.val] = (TC_Entry){
                        .known_temporal = true,
                        .type = (TC_typeid){
                            .ty = TC_TY_Primitive,
                            .info.primitive = prim,
                            // .full_typeuse_span = (TokenSpan){.sv = prim_sv}
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
                    instr_nth = instr_nth + 1;
                    // printf("todo: IR_INST_TY__ENUM_VARIANT_NUMBER_FROM_TYPE | type inference pass\n");
                    break;
                }
                case IR_INST_TY__MAKE_BOX: {
                    if (!temporals_map->array[instr.info.make_box.dest.val].known_temporal) break;
                    const TC_typeid dest_t = temporals_map->array[instr.info.make_box.dest.val].type;
                    if (dest_t.ty != TC_TY_Box) break;
                    if (temporals_map->array[instr.info.make_box.src.val].known_temporal) break;
                    temporals_map->array[instr.info.make_box.src.val] = (TC_Entry) {
                        .known_temporal = true,
                        .type = *dest_t.info.contained,
                        .is_value_known = false,
                        .is_addressable = false
                    };
                    break;
                }
            }
        }
    }
    // todo: fix external fn calls (do not infer types for them)
    free(ref_of_dests);
}