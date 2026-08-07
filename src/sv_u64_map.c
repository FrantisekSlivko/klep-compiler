#include <stdint.h>

#include "khash.h"
#include "string_view.h"
#include "sv_u64_map.h"

// 2. FNV-1a hash function
khint_t sv_hash_func(const SV sv) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < sv.len; ++i) {
        hash ^= (uint8_t)sv.start[i];
        hash *= 16777619u;
    }
    return (khint_t)hash;
}

// 3. Equality function
int sv_hash_equal(const SV a, const SV b) {
    return (a.len == b.len) && (memcmp(a.start, b.start, a.len) == 0);
}