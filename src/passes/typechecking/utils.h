
#ifndef KLEPCOMPILER_UTIL_H
#define KLEPCOMPILER_UTIL_H

#include "passes/inference_check_types.h"

SizeAlign calc_size_of(const TC_typeid* type, const TC_TypeTable* restrict nct,
                       const TargetInfo* restrict target_info);

#endif //KLEPCOMPILER_UTIL_H
