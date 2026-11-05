
#ifndef KLEPCOMPILER_VARIABLE_STORAGE_PROMOTION_H
#define KLEPCOMPILER_VARIABLE_STORAGE_PROMOTION_H

#include "passes/inference_check_types.h"

void variable_storage_promotion_pass(
    const IR_func fn,
    khash_t(sv_u64_map)* restrict identifier_map,
    const TC_Entry_LIST temporals,
    IR_temporal_LIST* restrict promote_to_stack_temps,
    const TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info
);



#endif //KLEPCOMPILER_VARIABLE_STORAGE_PROMOTION_H
