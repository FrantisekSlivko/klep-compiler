#ifndef KLEPCOMPILER_FLOAT64_H
#define KLEPCOMPILER_FLOAT64_H

#include <assert.h>
#include <float.h>

static_assert(DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024, "double is not IEEE 754 binary64 on this platform");

typedef double float64_t;

#endif //KLEPCOMPILER_FLOAT64_H
