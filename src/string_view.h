
#ifndef KLEPCOMPILER_STRING_VIEW_H
#define KLEPCOMPILER_STRING_VIEW_H

#include "list.h"
#include "utils/float64.h"

typedef struct {
    const char* start;
    size_t len;
} SV;

SV SV_lrslice_from_SV(const SV* sv, size_t start_index, size_t len);

SV SV_from_str(const char*);
SV SV_from_str_len(const char*, size_t);

bool SV_pp_cmp_eq(const SV* restrict, const SV* restrict);
bool SV_pv_cmp_eq(const SV* restrict, const char* restrict, size_t);

bool SV_to_int64_copybased(SV sv, int64_t *out);
bool SV_to_float64_copybased(const SV sv, float64_t *out);


init_list_type(SV, SVList)

#define SV_format "%.*s"
#define SV_p_args(X) ((int)((X)->len)), ((X)->start)
#define SV_v_args(X) ((int)((X).len)), ((X).start)

#endif //KLEPCOMPILER_STRING_VIEW_H
