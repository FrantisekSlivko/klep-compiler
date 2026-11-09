
#ifndef KLEPCOMPILER_MATERIALIZATION_H
#define KLEPCOMPILER_MATERIALIZATION_H


#include "ir.h"
#include "typecompletion.h"

const IR_func* materialize_function(
    const IR_generic_function* restrict gf,
    TC_typeid_LIST found_args_t,
    const TC_typeid* found_ret_t,
    TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info,
    const khash_t(sv_u64_map)* restrict identifier_map
);

#endif //KLEPCOMPILER_MATERIALIZATION_H
