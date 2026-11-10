#include "resolve.h"

#include "passes/type_utils.h"
#include "passes/typechecking/materialization.h"
#include "passes/typechecking/perf.h"
#include "typeresolve/type_resolve.h"


/// scheme:  FN__O{overload_nth}__{name}
SV mangle_function_name(
    const SpannedIdentifier* restrict fn_name,
    const size_t overload_nth
) {
    char* rs = malloc(sizeof(char)*1024);
    if (rs==NULL) INTERNAL_ERROR("NULL MALLOC")
    const int len = snprintf(rs, 1024, "FN__O%lu__"SV_format"", overload_nth, SV_v_args(fn_name->span.sv));
    const char* rs2 = realloc(rs, sizeof(char)*(len+1));
    if (rs2==NULL) INTERNAL_ERROR("NULL MALLOC")
    return (SV){.start = rs2, .len = len};
}

bool cmp_IR_typeuse(const IR_typeuse* restrict a, const IR_typeuse* restrict b) {
    if (a->tt_ty != b->tt_ty) return false;
    switch (a->tt_ty) {
        case IR_TY_Void: return true;
        case IR_TY_Primitive: return a->info.primitive == b->info.primitive;
        case IR_TY_Named: return a->info.named.identifier == b->info.named.identifier;
        case IR_TY_Ref:
        case IR_TY_Box:
        case IR_TY_List:
        case IR_TY_Slice: {
            return cmp_IR_typeuse(a->contained, b->contained);
        }
        case IR_TY_Array: {
            if (a->array_const_size != b->array_const_size) return false;
            return cmp_IR_typeuse(a->contained, b->contained);
        }
        case IR_TY_Generic: {
            if (a->info.named.identifier != b->info.named.identifier) return false;
            if (a->contained_len != b->contained_len) return false;
            for (size_t n = 0; n < a->contained_len; n++) {
                if (!cmp_IR_typeuse(&a->contained[n], &b->contained[n])) return false;
            }
            return true;
        }
    }
    UNREACHABLE()
}

static OverloadMatchRank score_argument_compatibility(
    const IR_temporal arg_temp,
    const TC_typeid* arg_t,
    const TC_typeid* param_t,
    const TC_Entry_LIST* temporals_map,
    const TC_Value_LIST* temporals_map_values
) {
    if (is_type_eq(arg_t, param_t)) {
        return OVERLOAD_MATCH_EXACT;
    }
    // auto-deref
    TC_typeid unwrapped_arg_t = *arg_t;
    while (unwrapped_arg_t.ty == TC_TY_Ref) {
        unwrapped_arg_t = *unwrapped_arg_t.info.contained;
    }
    if (is_type_eq(&unwrapped_arg_t, param_t)) {
        return OVERLOAD_MATCH_EXACT;
    }
    // only exact match or auto-ref addressable values
    if (param_t->ty == TC_TY_Ref) {
        if (is_type_eq(param_t->info.contained, &unwrapped_arg_t) &&
            temporals_map->array[arg_temp.val].is_addressable) {
            return OVERLOAD_MATCH_AUTO_REF;
            }
        return OVERLOAD_MATCH_INCOMPATIBLE;
    }
    // widening and constant narrowing
    if (can_be_implicit_casted(&unwrapped_arg_t, param_t)) {
        return OVERLOAD_MATCH_WIDENING;
    }
    if (can_const_narrow(arg_temp.val, param_t, temporals_map, temporals_map_values)) {
        return OVERLOAD_MATCH_CONST_NARROW;
    }
    return OVERLOAD_MATCH_INCOMPATIBLE;
}

static size_t rank_to_penalty(const OverloadMatchRank rank) {
    switch (rank) {
        case OVERLOAD_MATCH_EXACT: {
            return 0;
        }
        case OVERLOAD_MATCH_WIDENING: {
            return 1;
        }
        case OVERLOAD_MATCH_CONST_NARROW: {
            return 2;
        }
        case OVERLOAD_MATCH_AUTO_REF: {
            return 3;
        }
        case OVERLOAD_MATCH_INCOMPATIBLE: {
            return SIZE_MAX;
        }
    }
    UNREACHABLE()
}

/// returns true if for all n: rank_to_penalty(a->arg_ranks[n]) <= rank_to_penalty(b->arg_ranks[n])
/// AND
/// for at least one n: rank_to_penalty(a->arg_ranks[n]) < rank_to_penalty(b->arg_ranks[n]).
static bool candidate_dominates(const OverloadCandidate* restrict a, const OverloadCandidate* restrict b, const size_t num_args) {
    bool one_less = false;
    for (size_t n = 0; n < num_args; n++) {
        if (rank_to_penalty(a->arg_ranks[n]) > rank_to_penalty(b->arg_ranks[n])) {
            return false;
        }
        if (rank_to_penalty(a->arg_ranks[n]) < rank_to_penalty(b->arg_ranks[n])) {
            one_less = true;
        }
    }
    return one_less;
}


void free_overload_resolution_result(const OverloadResolutionResult* restrict result) {
    if (result->candidates_count > 0 && result->candidates != NULL) {
        for (size_t n = 0; n < result->candidates_count; n++) {
            free(result->candidates[n].arg_ranks);
        }
        free(result->candidates);
    }
}



OverloadResolutionResult resolve_function_overload_concreate(
    const Identifier fn_name,
    const IR_temporal_LIST* restrict raw_args,
    const TC_Entry_LIST* restrict temporals_map,
    const IR_func_LIST* restrict functions,
    TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info,
    const khash_t(sv_u64_map)* restrict identifier_map,
    const TC_Value_LIST* restrict temporals_map_values
) {

    OverloadCandidate_LIST overload_candidates = OverloadCandidate_LIST_new();



    for (size_t n = 0; n < functions->len; n++) {
        const IR_func* candidate = &functions->array[n];
        if (candidate->fn_name.identifier != fn_name) continue;
        if (candidate->args.len != raw_args->len) {
            OverloadCandidate_LIST_push(&overload_candidates, (OverloadCandidate) {
                .fn = candidate,
                .arg_ranks = NULL,
                .total_penalty = SIZE_MAX,
                .is_viable = false
            });
            continue;
        }
        if (candidate->args.len == 0) {
            OverloadCandidate_LIST_push(&overload_candidates, (OverloadCandidate) {
                .fn = candidate,
                .arg_ranks = NULL,
                .total_penalty = 0,
                .is_viable = true
            });
            continue;
        }

        OverloadMatchRank* mr = malloc(sizeof(OverloadMatchRank)*raw_args->len);
        bool is_viable = true;
        size_t total_penalty = 0;
        for (size_t a = 0; a < raw_args->len; a++) {
            const IR_temporal arg_temp = raw_args->array[a];
            const TC_typeid* arg_t = &temporals_map->array[arg_temp.val].type;
            const TC_typeid param_t = resolve_tc_type(
                &candidate->args.array[a].type,
                nct,
                target_info,
                source_info,
                identifier_map
            );
            const OverloadMatchRank rank = score_argument_compatibility(
                arg_temp,
                arg_t,
                &param_t,
                temporals_map,
                temporals_map_values
            );
            mr[a] = rank;
            if (rank == OVERLOAD_MATCH_INCOMPATIBLE) {
                is_viable = false;
            } else if (is_viable) {
                total_penalty += rank_to_penalty(rank);
            }
        }
        OverloadCandidate_LIST_push(&overload_candidates, (OverloadCandidate) {
            .fn = candidate,
            .arg_ranks = mr,
            .total_penalty = is_viable ? total_penalty : SIZE_MAX,
            .is_viable = is_viable
        });
    }

    OverloadCandidate_LIST_shrink(&overload_candidates);
    if (overload_candidates.len == 0) {
        return (OverloadResolutionResult) {
            .status = OVERLOAD_RESULT_NO_MATCH,
            .resolved_fn = NULL,
            .candidates = NULL,
            .candidates_count = 0
        };
    }


    size_t arity_match_count = 0;
    size_t viable_count = 0;
    OverloadCandidate* viable = malloc(sizeof(OverloadCandidate)*overload_candidates.len);
    for (size_t i = 0; i < overload_candidates.len; i++) {
        if (overload_candidates.array[i].fn->args.len == raw_args->len) {
            arity_match_count++;
        }
        if (overload_candidates.array[i].is_viable) {
            viable[viable_count] = overload_candidates.array[i];
            viable_count++;
        }
    }

    if (arity_match_count == 0) {
        return (OverloadResolutionResult) {
            .status = OVERLOAD_RESULT_ARITY_MISMATCH,
            .resolved_fn = NULL,
            .candidates = overload_candidates.array,
            .candidates_count = overload_candidates.len
        };
    }

    // Functions with matching arity exist, but none are type-compatible.
    if (viable_count == 0) {
        return (OverloadResolutionResult) {
            .status = OVERLOAD_RESULT_NO_MATCH,
            .resolved_fn = NULL,
            .candidates = overload_candidates.array,
            .candidates_count = overload_candidates.len
        };
    }

    if (viable_count == 1) {
        return (OverloadResolutionResult) {
            .status = OVERLOAD_RESULT_SUCCESS,
            .resolved_fn = viable[0].fn,
            .candidates = NULL,
            .candidates_count = 0
        };
    }

    // • Multiple Viable Candidates: If viable_count > 1:
    // • Hand off to Step 3 for pairwise candidate dominance comparison.
    const OverloadCandidate* dominates_all = NULL;
    bool multy_dominate_all = false;

    for (size_t j = 0; j < viable_count; j++) {
        OverloadCandidate* a = &viable[j];

        bool dominate_all_a = true;
        for (size_t k = 0; k < viable_count; k++) {
            if (j == k) continue;
            const OverloadCandidate* b = &viable[k];

            if (!candidate_dominates(a, b, raw_args->len)) {
                dominate_all_a = false;
                break;
            }
        }
        if (dominate_all_a) {
            if (dominates_all == NULL) {
                dominates_all = a;
            } else {
                multy_dominate_all = true;
                break;
            }
        }
    }

    if (multy_dominate_all || dominates_all == NULL) {
        return (OverloadResolutionResult) {
            .status = OVERLOAD_RESULT_AMBIGUOUS,
            .resolved_fn = NULL,
            .candidates = viable,
            .candidates_count = viable_count
        };
    }

    return (OverloadResolutionResult) {
        .status = OVERLOAD_RESULT_SUCCESS,
        .resolved_fn = dominates_all->fn,
        .candidates = NULL,
        .candidates_count = 0
    };
}


/// basicly depth of specification
uint64_t calc_type_specificity(const IR_typeuse* t) {
    switch (t->tt_ty) {
        case IR_TY_Void: {
            return 1;
        }
        case IR_TY_Primitive:
        case IR_TY_Named: {
            return 1;
        }
        case IR_TY_Ref:
        case IR_TY_Box:
        case IR_TY_Slice:
        case IR_TY_List:
        case IR_TY_Array: {
            return 1 + calc_type_specificity(t->contained);
        }
        case IR_TY_Generic: {
            uint64_t max = 0;
            for (size_t n = 0; n < t->contained_len; n++) {
                const uint64_t v = calc_type_specificity(&t->contained[n]);
                if (v > max) {
                    max = v;
                }
            }
            return max + 1;
        }
    }
    UNREACHABLE()
}

static bool generic_candidate_dominates(const IR_generic_function* a, const IR_generic_function* b, const khash_t(sv_u64_map)* restrict identifier_map) {
    bool one_less = false;
    // remake to higher the better
    for (size_t n = 0; n < a->fn.args.len; n++) {
        const uint64_t as = calc_type_specificity(&a->fn.args.array[n].type);
        const uint64_t bs = calc_type_specificity(&b->fn.args.array[n].type);
        // printf("generic_candidate_dominates calc %lu arg -> a=%lu vs b=%lu  | where a_type = '", n, as, bs);
        // print_IR_typeuse(&a->fn.args.array[n].type, identifier_map);
        // printf("', b_type = '");
        // print_IR_typeuse(&b->fn.args.array[n].type, identifier_map);
        // printf("'\n");
        if (as < bs) {
            return false;
        }
        if (as > bs) {
            one_less = true;
        }
    }
    // printf("result = %u\n", one_less);
    return one_less;
}

OverloadResolutionResult resolve_function_overload(
    const Identifier fn_name,
    const IR_temporal_LIST* restrict raw_args,
    const IR_temporal raw_return_v,
    const TC_Entry_LIST* restrict temporals_map,
    const IR_func_LIST* restrict functions,
    TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info,
    const khash_t(sv_u64_map)* restrict identifier_map,
    const TC_Value_LIST* restrict temporals_map_values
) {
    const OverloadResolutionResult concreate_result = resolve_function_overload_concreate(
        fn_name,
        raw_args,
        temporals_map,
        functions,
        nct,
        target_info,
        source_info,
        identifier_map,
        temporals_map_values
    );

    if (concreate_result.status == OVERLOAD_RESULT_SUCCESS) {
        return concreate_result;
    }

    // check generic and rank
    TC_typeid_LIST args = TC_typeid_LIST_new_with_capacity(raw_args->len);
    for (size_t a = 0; a < raw_args->len; a++) {
        if (temporals_map->array[raw_args->array[a].val].known_temporal) {
            TC_typeid_LIST_push(&args, temporals_map->array[raw_args->array[a].val].type);
        } else {
            TC_typeid_LIST_push(&args, (TC_typeid){.ty = TC_TY_Void});
        }
    }

    size_t_LIST generic_rejected_candidates_arity = size_t_LIST_new();
    size_t_LIST generic_rejected_candidates_match = size_t_LIST_new();

    bool has_best_bound = false;
    // TC_GenericArgMap_LIST best_bound_map;
    IR_generic_function* best_found_fn = NULL;

    for (size_t g = 0; g < nct->generic_functions.len; g++) {
        IR_generic_function* gf = &nct->generic_functions.array[g];
        if (gf->fn.fn_name.identifier != fn_name) {
            continue;
        }
        if (gf->fn.args.len != raw_args->len) {
            size_t_LIST_push(&generic_rejected_candidates_arity, g);
            continue;
        }


        TC_GenericArgMap_LIST map = TC_GenericArgMap_LIST_new_with_capacity(gf->generic_args.len);

        const bool b = try_match_generic_function_args(
            gf,
            args,
            temporals_map->array[raw_return_v.val].known_temporal ? &temporals_map->array[raw_return_v.val].type : &(TC_typeid){.ty = TC_TY_Void},
            &map,
            nct,
            target_info,
            source_info,
            identifier_map
        );
        if (b) {
            // look at `map` and compare if more specific than current best match
            if (!has_best_bound) {
                // best_bound_map = map;
                best_found_fn = gf;
                has_best_bound = true;
            } else {
                // compare what is more specific, this or `best_bound_map`
                if (generic_candidate_dominates(gf, best_found_fn, identifier_map)) {
                    best_found_fn = gf;
                    // best_bound_map = map;
                }
            }
        } else {
            TC_GenericArgMap_LIST_free(&map);
            size_t_LIST_push(&generic_rejected_candidates_match, g);
        }
    }

    // best_bound_map = TC_GenericArgMap_LIST_new_with_capacity(best_bound_map.len * 0);

    if (has_best_bound) {
        const TC_typeid found_ret_t = temporals_map->array[raw_return_v.val].type;
        const IR_func* out_fn = materialize_function(best_found_fn, args, &found_ret_t, nct, target_info, source_info, identifier_map);
        return (OverloadResolutionResult) {
            .status = OVERLOAD_RESULT_SUCCESS,
            .resolved_fn = out_fn,
            .candidates = NULL,
            .candidates_count = 0
        };
    }

    return concreate_result;
}