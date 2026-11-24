#ifndef KLEPCOMPILER_ARGS_H
#define KLEPCOMPILER_ARGS_H
#include "string_view.h"


typedef struct {
    SV input_filename;
    const char* raw_input_filename;
    bool input_provided;
    SV output_filename;
    bool output_provided;

    SV emit_ssa_path;
    bool emit_ssa_provided;

    SV emit_asm_path;
    bool emit_asm_provided;

    SV qbe_bin;
    SV cc_bin;
    SV qbe_target;
    SV cc_args;
} CompilerArgs;

CompilerArgs parse_args(const SVList* restrict args_list, const SV program_name);


#endif //KLEPCOMPILER_ARGS_H
