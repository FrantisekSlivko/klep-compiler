
#include "string_view.h"

#include <assert.h>
#include <errno.h>
#include <string.h>

#include "utils/float64.h"

SV SV_lrslice_from_SV(const SV* sv, const size_t start_index, const size_t len) {
    assert(sv->len >= (start_index + len));
    return (SV) { .start = (sv->start)+sizeof(char)*start_index, .len = len };
}


SV SV_from_str_len(const char* s, const size_t len) {
    return (SV) { .start = s, .len = len };
}

SV SV_from_str(const char* s) {
    return SV_from_str_len(s, strlen(s));
}

bool SV_pp_cmp_eq(const SV* restrict a, const SV* restrict b) {
    // guard against null
    if (!a || !b) return false;

    if (a->len != b->len) return false;

    // null buffer
    if (!a->start || !b->start) return false;

    return memcmp(a->start, b->start, a->len) == 0;
}

bool SV_pv_cmp_eq(const SV* restrict a, const char* restrict s, const size_t l) {
    // guard against null
    if (!a || !s) return false;

    if (a->len != l) return false;

    if (!a->start) return false;

    return memcmp(a->start, s, l) == 0;
}


bool SV_to_int64_copybased(const SV sv, int64_t *out) {
    // 64-bit int string length is at most 20 chars + 1 sign + 1 null byte = 22
    if (sv.len == 0 || sv.len > 64 || out == NULL) {
        return false;
    }

    char buf[65];
    memcpy(buf, sv.start, sv.len);
    buf[sv.len] = '\0';

    char *endptr;
    errno = 0;
    long long val = strtoll(buf, &endptr, 10);

    // Check for parsing errors, empty input, trailing characters, or overflow
    if (endptr == buf || *endptr != '\0' || errno == ERANGE) {
        return false;
    }

    *out = (int64_t)val;
    return true;
}

bool SV_to_float64_copybased(const SV sv, float64_t *out) {
    if (sv.len == 0 || sv.len > 64 || out == NULL) {
        return false;
    }

    char buf[65];
    memcpy(buf, sv.start, sv.len);
    buf[sv.len] = '\0';

    char *endptr;
    errno = 0;
    double val = strtod(buf, &endptr);

    // Check for parsing errors, empty input, trailing characters, or overflow/underflow
    if (endptr == buf || *endptr != '\0' || errno == ERANGE) {
        return false;
    }

    *out = (float64_t)val;
    return true;
}