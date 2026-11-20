#ifndef KLEPCOMPILER_TYPE_UTILS_H
#define KLEPCOMPILER_TYPE_UTILS_H

#include "passes/inference_check_types.h"

bool is_type_eq(const TC_typeid* a, const TC_typeid* b);


/// performs deep and full copy
TC_typeid TC_typeid_deep_copy(const TC_typeid* restrict src);

/// allocates TC_typeid and makes deep copy
TC_typeid* TC_typeid_new_copy(const TC_typeid* restrict src);

#endif //KLEPCOMPILER_TYPE_UTILS_H
