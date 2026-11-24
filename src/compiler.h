#ifndef KLEPCOMPILER_COMPILER_H
#define KLEPCOMPILER_COMPILER_H
#include "args.h"
#include "khash.h"
#include "source_info.h"
#include "sv_u64_map.h"
#include "target.h"


void main_compile(const TargetInfo target_info, const SourceInfo source_info, const CompilerArgs* restrict compiler_args);

typedef struct {
    const TargetInfo target_info;
    khash_t(sv_u64_map)* identifier_map;
    uint64_t identifier_map_cnt;
    /// absolute path to ModulePathId
    khash_t(sv_u64_map)* absolute_file_path_map;
} GlobalCompilerState;




#endif //KLEPCOMPILER_COMPILER_H
