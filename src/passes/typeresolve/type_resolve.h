
#ifndef KLEPCOMPILER_TYPE_RESOLVE_H
#define KLEPCOMPILER_TYPE_RESOLVE_H

#include "passes/inference_check_types.h"


TC_typeid resolve_tc_type(
    const IR_typeuse* restrict type,
    TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info
);

SizeAlign path_size_align_for_materialized_type(
    const TC_typeid* restrict type,
    const TC_TypeTable* restrict nct,

    size_t_LIST* restrict in_progress_structs,
    size_t_LIST* restrict in_progress_enums,

    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info
);



#endif //KLEPCOMPILER_TYPE_RESOLVE_H
