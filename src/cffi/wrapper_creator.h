#ifndef KLEPCOMPILER_WRAPPER_CREATOR_H
#define KLEPCOMPILER_WRAPPER_CREATOR_H
#include "mir.h"


MIR_func create_wrap_cffi_fn(const CFFI_IR_extern_fn fn, TC_TypeTable* restrict nct, const TargetInfo* restrict target_info, const SourceInfo* restrict source_info, const khash_t(sv_u64_map)* restrict identifier_map);


#endif //KLEPCOMPILER_WRAPPER_CREATOR_H
