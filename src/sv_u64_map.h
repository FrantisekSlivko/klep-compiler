//
// Created by frantisek on 3. 8. 2026.
//

#ifndef KLEPCOMPILER_SV_U64_MAP_H
#define KLEPCOMPILER_SV_U64_MAP_H
#include <stdint.h>

#include "khash.h"
#include "string_view.h"


khint_t sv_hash_func(SV sv);
int sv_hash_equal(SV a, SV b);

// KHASH_DECLARE(sv_u64_map, SV, uint64_t)
// KHASH_INIT(sv_u64_map, SV, uint64_t, 1, sv_hash_func, sv_hash_equal)
KHASH_INIT2(sv_u64_map, static inline, SV, uint64_t, 1, sv_hash_func, sv_hash_equal)

#endif //KLEPCOMPILER_SV_U64_MAP_H
