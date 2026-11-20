
#ifndef KLEPCOMPILER_MIR_PROCESS_H
#define KLEPCOMPILER_MIR_PROCESS_H

#include "passes/inference_check_types.h"
#include "mir.h"


MIR_func TC_process_fn(
    const IR_func fn,
    khash_t(sv_u64_map)* restrict identifier_map,
    const IR_func_LIST* restrict functions,
    const CFFI_IR_extern_fn_LIST* restrict external_functions,
    TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info,
    TC_typeid_LIST* restrict type_needing_destructor
);

MIR_func_LIST TC_typecheck_and_lower(
    khash_t(sv_u64_map)* restrict identifier_map,
    const IR_func_LIST* restrict functions,
    const CFFI_IR_extern_fn_LIST* restrict external_functions,
    TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info,
    TC_typeid_LIST* restrict type_needing_destructor
);

#endif //KLEPCOMPILER_MIR_PROCESS_H
