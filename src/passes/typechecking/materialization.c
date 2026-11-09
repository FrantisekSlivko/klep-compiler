#include "materialization.h"

#include "passes/type_utils.h"
#include "passes/typeresolve/type_resolve.h"


/// todo: USE PROPERLY (NOT USED NOW)
IR_typeuse typeuse_from_tc(const TC_typeid* tc, const TC_TypeTable* restrict nct) {
    switch (tc->ty) {
        case TC_TY_Void: {
            return (IR_typeuse){
                .tt_ty = IR_TY_Void,
                // .full_typeuse_span = tc->full_typeuse_span
            };
        }
        case TC_TY_Primitive: {
            return (IR_typeuse){
                .tt_ty = IR_TY_Primitive,
                .info.primitive = tc->info.primitive,
                // .full_typeuse_span = tc->full_typeuse_span
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
                // .full_typeuse_span = tc->full_typeuse_span
            };
        }
        case TC_TY_Array: {
            IR_typeuse* inner = malloc(sizeof(IR_typeuse));
            *inner = typeuse_from_tc(tc->info.t_array.contained, nct);
            return (IR_typeuse){
                .tt_ty = IR_TY_Array,
                .contained = inner,
                .array_const_size = tc->info.t_array.array_size,
                // .full_typeuse_span = tc->full_typeuse_span
            };
        }
        case TC_TY_Struct:
        case TC_TY_Enum: {


            // // 1. Check if this struct/enum was a materialized generic
            // for (size_t n = 0; n < nct->materialized_generics.len; n++) {
            //     const TC_materialized_generic_index_record mr = nct->materialized_generics.array[n];
            //     if (mr.nth == tc->info.type_id) {
            //         if ((tc->ty == TC_TY_Struct && mr.kind == TC_NIRK_Struct) ||
            //             (tc->ty == TC_TY_Enum && mr.kind == TC_NIRK_Enum)) {
            //             return mr.generic; // Returns the original IR_typeuse with generic args!
            //         }
            //     }
            // }
            const bool was_generic = (tc->ty == TC_TY_Struct) ? nct->structs.array[tc->info.type_id].from_generic : nct->enums.array[tc->info.type_id].from_generic;
            if (was_generic) {
                const size_t n = (tc->ty == TC_TY_Struct) ? nct->structs.array[tc->info.type_id].generic_cache_index : nct->enums.array[tc->info.type_id].generic_cache_index;
                const TC_materialized_generic_index_record mr = nct->materialized_generics.array[n];
                return mr.generic;
            }

            // 2. Otherwise, it's a regular non-generic struct/enum
            const SpannedIdentifier name = (tc->ty == TC_TY_Struct)
                                               ? nct->structs.array[tc->info.type_id].name
                                               : nct->enums.array[tc->info.type_id].name;

            return (IR_typeuse){
                .tt_ty = IR_TY_Named,
                .info.named = name,
                // .full_typeuse_span = tc->full_typeuse_span
            };
        }
        case TC_TY_External_Type: {
            return (IR_typeuse){
                .tt_ty = IR_TY_Named,
                .info.named = tc->info.external_type,
                // .full_typeuse_span = tc->full_typeuse_span
            };
        }
    }


    UNREACHABLE()
}

// void find_type_param_parallel_ir(
//     const IR_typeuse* restrict t1,
//     const IR_typeuse* restrict t2,
//     const SpannedIdentifier_LIST* restrict generic_args,
//     TC_GenericArgMap_LIST* restrict out,
//     const TC_TypeTable* restrict nct
// ) {
//
// }

/// returns true if found
void find_type_param_parallel(
    const IR_typeuse* restrict t1,
    const TC_typeid* restrict t2,
    const SpannedIdentifier_LIST* restrict generic_args,
    TC_GenericArgMap_LIST* restrict out,
    TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info,
    const khash_t(sv_u64_map)* restrict identifier_map
) {

    if (t2->ty == TC_TY_Void) return; // used for function calls discarding return values
    switch (t1->tt_ty) {
        case IR_TY_Void: return;
        case IR_TY_Primitive: return;
        case IR_TY_Named: {
            for (size_t n = 0; n < generic_args->len; n++) {
                if (generic_args->array[n].identifier == t1->info.named.identifier) {
                    // t2->ty
                    bool found
                     = false;
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
            // const TC_typeid* nx = t2->ty == TC_TY_Ref ? t2->info.contained : t2;
            // if (t2->ty != TC_TY_Ref) {
            //     INTERNAL_ERROR("type error here")
            // }
            // find_type_param_parallel(t1->contained, nx, generic_args, out, nct);
            if (t2->ty != TC_TY_Box) {
                INTERNAL_ERROR("type error here")
            }
            find_type_param_parallel(t1->contained, t2->info.contained, generic_args, out, nct, target_info, source_info, identifier_map);
            break;
        }
        case IR_TY_Box: {
            if (t2->ty != TC_TY_Box) {
                INTERNAL_ERROR("type error here")
            }
            find_type_param_parallel(t1->contained, t2->info.contained, generic_args, out, nct, target_info, source_info, identifier_map);
            break;
        }
        case IR_TY_Slice: {
            if (t2->ty != TC_TY_Slice) {
                INTERNAL_ERROR("type error here")
            }
            find_type_param_parallel(t1->contained, t2->info.contained, generic_args, out, nct, target_info, source_info, identifier_map);
            break;
        }
        case IR_TY_Array: {
            if (t2->ty != TC_TY_Array) {
                INTERNAL_ERROR("type error here")
            }
            if (t1->array_const_size != t2->info.t_array.array_size) {
                INTERNAL_ERROR("type error here")
            }
            find_type_param_parallel(t1->contained, t2->info.t_array.contained, generic_args, out, nct, target_info, source_info, identifier_map);
            break;
        }
        case IR_TY_List: {
            if (t2->ty != TC_TY_List) {
                INTERNAL_ERROR("type error here")
            }
            find_type_param_parallel(t1->contained, t2->info.contained, generic_args, out, nct, target_info, source_info, identifier_map);
            break;
        }
        case IR_TY_Generic: {
            if (t2->ty != TC_TY_Enum && t2->ty != TC_TY_Struct) {
                INTERNAL_ERROR("type error here")
            }

            const size_t t2_type_id = t2->info.type_id;
            const bool from_generics = (t2->ty == TC_TY_Struct) ? nct->structs.array[t2_type_id].from_generic : nct->enums.array[t2_type_id].from_generic;
            if (!from_generics) {
                INTERNAL_ERROR("type error here")
            }
            const size_t mtr_index = (t2->ty == TC_TY_Struct) ? nct->structs.array[t2_type_id].generic_cache_index : nct->enums.array[t2_type_id].generic_cache_index;
            if (mtr_index >= nct->materialized_generics.len) {
                INTERNAL_ERROR()
            }
            const TC_materialized_generic_index_record* record = &nct->materialized_generics.array[mtr_index];
            if (record->kind == TC_NIRK_Struct && t2->ty != TC_TY_Struct) {
                INTERNAL_ERROR("type error here")
            } else if (record->kind == TC_NIRK_Enum && t2->ty != TC_TY_Enum) {
                INTERNAL_ERROR("type error here")
            }


            if (t1->contained_len != record->generic.contained_len) {
                INTERNAL_ERROR("type error here")
            }

            for (size_t n = 0; n < t1->contained_len; n++) {
                const TC_typeid tt = resolve_tc_type(&record->generic.contained[n], nct, target_info, source_info, identifier_map);
                find_type_param_parallel(&t1->contained[n], &tt, generic_args, out, nct, target_info, source_info, identifier_map);
            }

            break;

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
                // .full_typeuse_span = use->full_typeuse_span,
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
                // .full_typeuse_span = use->full_typeuse_span,
            };
        }

        case IR_TY_Generic: {
            UNIMPLEMENTED()
        }
    }
    UNREACHABLE()
}

const IR_func* materialize_function(
    const IR_generic_function* restrict gf,
    const TC_typeid_LIST found_args_t,
    const TC_typeid* found_ret_t, TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info,
    const khash_t(sv_u64_map)* restrict identifier_map
) {
    // find type args in functions args+ret -> in parallel, unwrap IR_typeuse and TC_typeid types
    TC_GenericArgMap_LIST map = TC_GenericArgMap_LIST_new_with_capacity(gf->generic_args.len);

    find_type_param_parallel(&gf->fn.fn_ret_t, found_ret_t, &gf->generic_args, &map, nct, target_info, source_info, identifier_map);
    for (size_t n = 0; n < found_args_t.len; n++) {
        if (map.len == gf->generic_args.len) break;
        find_type_param_parallel(&gf->fn.args.array[n].type, &found_args_t.array[n], &gf->generic_args, &map, nct, target_info, source_info, identifier_map);
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
                    break;
                }
                case IR_INST_TY__MAKE_STRUCT: {
                    const IR_typeuse out = materialization_fn_type_patch(instr.info.make_struct.type, &map, nct);
                    instr.info.make_struct.type = malloc(sizeof(IR_typeuse));
                    *instr.info.make_struct.type = out;
                    break;
                }
                case IR_INST_TY__MAKE_ENUM: {
                    const IR_typeuse out = materialization_fn_type_patch(instr.info.make_enum.type, &map, nct);
                    instr.info.make_enum.type = malloc(sizeof(IR_typeuse));
                    *instr.info.make_enum.type = out;
                    break;
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



