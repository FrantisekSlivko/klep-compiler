#ifndef KLEPCOMPILER_PERF_H
#define KLEPCOMPILER_PERF_H

#include "passes/inference_check_types.h"

#include "utils.h"


IR_temporal use_value(
    const IR_temporal t,
    size_t* restrict highest_temporal,
    TC_Entry_LIST* restrict temporals_map,
    const IR_temporal_LIST* promoted,
    const TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info
);


IR_temporal_and_TC_type perf_auto_deref(
    const TC_typeid* type,
    const IR_temporal* intake,
    size_t* highest_temporal,
    TC_Entry_LIST* temporals_map,
    const IR_temporal_LIST* promoted,
    const TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info
);

TC_typeid get_common_numeric_type(const TC_typeid t1, const TC_typeid t2);

bool can_const_narrow(const uint64_t temporal, const TC_typeid* to, const TC_Entry_LIST* temporals_map,
                      const TC_Value_LIST* temporals_map_values);

bool can_be_implicit_casted(const TC_typeid* from, const TC_typeid* to);

DoubleIR_temporal_and_TC_typeid perf_numeric_promotion(
    const IR_temporal* a,
    const TC_typeid* a_type,
    const IR_temporal* b,
    const TC_typeid* b_type,
    size_t* highest_temporal,
    bool* is_out_error,
    bool* is_out_error_incompatible_number_relation,
    TC_Entry_LIST* temporals_map,
    const TC_Value_LIST* temporals_map_values,
    const IR_temporal_LIST* promoted,
    const TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info
);


IR_temporal perf_bool(
    const IR_temporal* intake,
    const TC_typeid* intake_type,
    size_t* highest_temporal,
    TC_Entry_LIST* temporals_map,
    const IR_temporal_LIST* promoted,
    const TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info
);


IR_temporal_and_TC_type perf_auto_ref(
    const IR_temporal* intake,
    const TC_typeid* intake_type,
    size_t* highest_temporal,
    TC_Entry_LIST* temporals_map
);


IR_temporal perf_autos(
    const IR_temporal* restrict intake,
    const TC_typeid* intake_type,
    const TC_typeid* output_type,
    size_t* restrict highest_temporal,
    const khash_t(sv_u64_map)* restrict identifier_map,
    const TC_TypeTable* nct,
    TC_Entry_LIST* restrict temporals_map,
    const SourceInfo* restrict source_info,
    const SV* value_token,
    TC_Value_LIST* temporals_map_values,
    const IR_temporal_LIST* promoted,
    const TargetInfo* restrict target_info
);

bool can_be_explicit_casted(const TC_typeid* from, const TC_typeid* to);

#endif //KLEPCOMPILER_PERF_H
