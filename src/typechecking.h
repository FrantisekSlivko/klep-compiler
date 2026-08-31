
#ifndef KLEPCOMPILER_TYPECHECKING_H
#define KLEPCOMPILER_TYPECHECKING_H

#include "typecompletion.h"

#include "ir.h"

#include "source_info.h"
#include "khash.h"
#include "sv_u64_map.h"


void TC_typecheck_and_lower(
    const khash_t(sv_u64_map)* restrict identifier_map,
    const IR_func_LIST* restrict functions,
    const CFFI_IR_extern_fn_LIST* restrict external_functions,
    TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info,
    TC_typeid_LIST* restrict type_needing_destructor
);

#endif //KLEPCOMPILER_TYPECHECKING_H
