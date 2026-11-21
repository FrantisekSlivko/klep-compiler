#ifndef KLEPCOMPILER_MIR_TO_QBE_H
#define KLEPCOMPILER_MIR_TO_QBE_H
#include "mir.h"





// void mir_types_to_qbe(const TC_TypeTable* restrict nct, TC_typeid_LIST* restrict out_table, FILE* restrict qbe_out);

void mir_fn_to_qbe(const MIR_func* restrict fn, const TC_TypeTable* restrict nct, TC_typeid_LIST* restrict qbe_mat_needing_types, khash_t(sv_u64_map)* restrict identifier_map, FILE* restrict qbe_out, const TargetInfo* restrict target_info, SVList* restrict global_string_const_table);

// void mir_emit_type_table(const TC_typeid_LIST* restrict qbe_mat_needing_types, const TC_TypeTable* restrict nct, FILE* restrict qbe_type_out, const TargetInfo* restrict target_info, const khash_t(sv_u64_map)* restrict identifier_map);
void mir_types_preregister(const TC_TypeTable* restrict nct, TC_typeid_LIST* restrict qbe_mat_needing_types, khash_t(sv_u64_map)* restrict identifier_map, FILE* restrict qbe_out, const TargetInfo* restrict target_info);

#endif //KLEPCOMPILER_MIR_TO_QBE_H
