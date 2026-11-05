
#ifndef KLEPCOMPILER_TYPEINFERENCE_H
#define KLEPCOMPILER_TYPEINFERENCE_H

#include "passes/inference_check_types.h"

void type_inference_pass_over_fn(
    const IR_func fn,
    khash_t(sv_u64_map)* restrict identifier_map,
    const IR_func_LIST* restrict functions,
    const CFFI_IR_extern_fn_LIST* restrict external_functions,
    TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info,
    TC_Entry_LIST* restrict temporals_map
);

static TC_structdef compiler_builtin__list_tc_structdef;
static TC_structdef compiler_builtin__slice_tc_structdef;

void init__compiler_builtin__tc_types(khash_t(sv_u64_map)* restrict identifier_map, uint64_t* restrict identifier_map_cnt, const TargetInfo* restrict target_info);

#endif //KLEPCOMPILER_TYPEINFERENCE_H
