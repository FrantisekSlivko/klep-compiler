
#ifndef KLEPCOMPILER_TYPE_RESOLVE_H
#define KLEPCOMPILER_TYPE_RESOLVE_H

#include "passes/inference_check_types.h"

typedef struct {
    size_t index;
    TC_index_record_kind kind;
} TC_incomplete;

init_prefixed_list_type(TC_incomplete)

TC_typeid resolve_tc_type(
    const IR_typeuse* restrict type,
    TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info,
    const khash_t(sv_u64_map)* restrict identifier_map
);

// SizeAlign path_size_align_for_materialized_type(
//     const TC_typeid* restrict type,
//     const TC_TypeTable* restrict nct,
//
//     // size_t_LIST* restrict in_progress_structs,
//     // size_t_LIST* restrict in_progress_enums,
//     TC_layout_dependency_LIST* restrict in_progress,
//
//     const TargetInfo* restrict target_info,
//     const SourceInfo* restrict source_info
// );



#endif //KLEPCOMPILER_TYPE_RESOLVE_H
