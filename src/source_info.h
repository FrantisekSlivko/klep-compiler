#ifndef KLEPCOMPILER_SOURCE_INFO_H
#define KLEPCOMPILER_SOURCE_INFO_H

#include <stdint.h>
// #include "compiler.h"
#include "string_view.h"


typedef uint64_t ModulePathId;

typedef struct {
    const SV source;
    const SV source_filename;
    const ModulePathId module_path_id;
} SourceInfo;

#endif //KLEPCOMPILER_SOURCE_INFO_H
