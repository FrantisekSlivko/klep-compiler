#ifndef KLEPCOMPILER_TYPECHECK_LOWERING_H
#define KLEPCOMPILER_TYPECHECK_LOWERING_H

#include "passes/inference_check_types.h"
#include "mir.h"


MIR_func lowering_and_emission_pass_over_fn(
    const IR_func fn,
    const khash_t(sv_u64_map)* restrict identifier_map,
    const IR_func_LIST* restrict functions,
    const CFFI_IR_extern_fn_LIST* restrict external_functions,
    TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info,
    TC_Entry_LIST* restrict temporals_map,
    const IR_temporal_LIST promote_to_stack_temps,
    TC_typeid_LIST* restrict type_needing_destructor
);

#endif //KLEPCOMPILER_TYPECHECK_LOWERING_H
