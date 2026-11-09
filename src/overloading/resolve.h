#ifndef KLEPCOMPILER_RESOLVE_H
#define KLEPCOMPILER_RESOLVE_H
#include "ir.h"
#include "passes/inference_check_types.h"


typedef enum {
    OVERLOAD_MATCH_INCOMPATIBLE = 0,
    OVERLOAD_MATCH_EXACT = 1,
    OVERLOAD_MATCH_WIDENING = 2,
    OVERLOAD_MATCH_CONST_NARROW = 3,
    OVERLOAD_MATCH_AUTO_REF = 4
} OverloadMatchRank;

typedef enum {
    OVERLOAD_RESULT_NO_MATCH,
    OVERLOAD_RESULT_SUCCESS,
    OVERLOAD_RESULT_AMBIGUOUS,
    OVERLOAD_RESULT_ARITY_MISMATCH
} OverloadResolutionStatus;

typedef struct {
    const IR_func* fn;
    OverloadMatchRank* arg_ranks; // array of ranks per argument
    size_t total_penalty;
    bool is_viable;
} OverloadCandidate;

init_prefixed_list_type(OverloadCandidate)

typedef struct {
    OverloadResolutionStatus status;
    const IR_func* resolved_fn;
    OverloadCandidate* candidates;
    size_t candidates_count;
} OverloadResolutionResult;


bool cmp_IR_typeuse(const IR_typeuse* restrict a, const IR_typeuse* restrict b);

SV mangle_function_name(
    const SpannedIdentifier* restrict fn_name,
    size_t overload_nth
);


void free_overload_resolution_result(const OverloadResolutionResult* restrict result);

OverloadResolutionResult resolve_function_overload(
    Identifier fn_name,
    const IR_temporal_LIST* restrict raw_args,
    const TC_Entry_LIST* restrict temporals_map,
    const IR_func_LIST* restrict functions,
    TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info,
    const khash_t(sv_u64_map)* restrict identifier_map,
    const TC_Value_LIST* restrict temporals_map_values
);





#endif //KLEPCOMPILER_RESOLVE_H
