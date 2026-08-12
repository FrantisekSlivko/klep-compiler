#ifndef KLEPCOMPILER_SOURCE_INFO_H
#define KLEPCOMPILER_SOURCE_INFO_H
#include "string_view.h"

typedef struct {
    const SV* restrict source;
    const SV* restrict source_filename;
} SourceInfo;

#endif //KLEPCOMPILER_SOURCE_INFO_H
