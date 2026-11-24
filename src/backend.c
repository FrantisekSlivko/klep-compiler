#include "backend.h"

#include <string.h>

#include "utils/dev.h"

#ifdef _WIN32
#define POPEN  _popen
#define PCLOSE _pclose
#define POPEN_MODE "wb"
#else
#define POPEN  popen
#define PCLOSE pclose
#define POPEN_MODE "w"
#endif

#define BACKEND_COMMAND_MAX_SIZE 2048

static const char* wrap_for_shell(const char* cmd) {
#ifdef _WIN32
    const size_t len = strlen(cmd);
    char* nw = malloc(sizeof(char)*(len+4));
    snprintf(nw, len+4, "\"%s\"", cmd );
    return nw;
#else
    return cmd;
#endif
    UNREACHABLE()
}

FILE* open_pipe(const char* command) {
    printf("[CMD] '%s'\n", command);
    const char* cm = wrap_for_shell(command);
    return POPEN(cm, POPEN_MODE);
}

void close_pipe(FILE* pipe) {
    PCLOSE(pipe);
}

int run_command(const char* command) {
    printf("[CMD] '%s'\n", command);
    const char* cm = wrap_for_shell(command);
    return system(cm);
}

FILE* backend_open(const CompilerArgs* restrict args) {
    char command[BACKEND_COMMAND_MAX_SIZE];

    // pipe all
    if (!args->emit_ssa_provided && !args->emit_asm_provided) {

        int len = snprintf(command, BACKEND_COMMAND_MAX_SIZE, SV_format, SV_v_args(args->qbe_bin));
        if (args->qbe_target.len != 0) {
            len += snprintf(&command[len], BACKEND_COMMAND_MAX_SIZE-len, " -t "SV_format" ", SV_v_args(args->qbe_target));
        }
        len += snprintf(&command[len], BACKEND_COMMAND_MAX_SIZE-len, " | ");
        len += snprintf(&command[len], BACKEND_COMMAND_MAX_SIZE-len, SV_format" -x assembler ", SV_v_args(args->cc_bin));
        if (args->cc_args.len != 0) {
            len += snprintf(&command[len], BACKEND_COMMAND_MAX_SIZE-len, SV_format" ", SV_v_args(args->cc_args));
        }
        if (args->output_provided) {
            len += snprintf(&command[len], BACKEND_COMMAND_MAX_SIZE-len, "-o \""SV_format"\" ", SV_v_args(args->output_filename));
        }
        snprintf(&command[len], BACKEND_COMMAND_MAX_SIZE-len, " - "); // gcc pipe
        return open_pipe(command);
    } else if (!args->emit_ssa_provided && args->emit_asm_provided) {
        int len = snprintf(command, BACKEND_COMMAND_MAX_SIZE, SV_format, SV_v_args(args->qbe_bin));
        if (args->qbe_target.len != 0) {
            len += snprintf(&command[len], BACKEND_COMMAND_MAX_SIZE-len, " -t "SV_format" ", SV_v_args(args->qbe_target));
        }
        snprintf(&command[len], BACKEND_COMMAND_MAX_SIZE-len, " -o \""SV_format"\" ", SV_v_args(args->emit_asm_path));
        return open_pipe(command);
    } else if (args->emit_ssa_provided) {
        snprintf(command, BACKEND_COMMAND_MAX_SIZE, SV_format, SV_v_args(args->emit_ssa_path));
        return fopen(command, "w+");
    } else {
        UNREACHABLE()
    }
    UNREACHABLE()
}


void backend_close(FILE* file, const CompilerArgs* restrict args) {
    if (args->emit_ssa_provided) {
        fclose(file);
    } else {
        close_pipe(file);
    }

    // run additional commands based on args
    // using run_command
    char command[BACKEND_COMMAND_MAX_SIZE];

    // read ssa and pipe qbe to gcc
    if (args->emit_ssa_provided && !args->emit_asm_provided) {

        // qbe .... <input_file> | gcc ....
        int len = snprintf(command, BACKEND_COMMAND_MAX_SIZE, SV_format, SV_v_args(args->qbe_bin));
        if (args->qbe_target.len != 0) {
            len += snprintf(&command[len], BACKEND_COMMAND_MAX_SIZE-len, " -t "SV_format" ", SV_v_args(args->qbe_target));
        }
        len += snprintf(&command[len], BACKEND_COMMAND_MAX_SIZE-len, " \""SV_format"\" ", SV_v_args(args->emit_ssa_path));
        len += snprintf(&command[len], BACKEND_COMMAND_MAX_SIZE-len, " | ");
        len += snprintf(&command[len], BACKEND_COMMAND_MAX_SIZE-len, SV_format" -x assembler ", SV_v_args(args->cc_bin));
        if (args->cc_args.len != 0) {
            len += snprintf(&command[len], BACKEND_COMMAND_MAX_SIZE-len, SV_format" ", SV_v_args(args->cc_args));
        }
        if (args->output_provided) {
            len += snprintf(&command[len], BACKEND_COMMAND_MAX_SIZE-len, "-o \""SV_format"\" ", SV_v_args(args->output_filename));
        }
        snprintf(&command[len], BACKEND_COMMAND_MAX_SIZE-len, " - "); // gcc pipe
        run_command(command);
    } else if (!args->emit_ssa_provided && args->emit_asm_provided) {
        // gcc <asm>

        int len = snprintf(command, BACKEND_COMMAND_MAX_SIZE, SV_format" -x assembler ", SV_v_args(args->cc_bin));
        if (args->cc_args.len != 0) {
            len += snprintf(&command[len], BACKEND_COMMAND_MAX_SIZE-len, SV_format" ", SV_v_args(args->cc_args));
        }
        if (args->output_provided) {
            len += snprintf(&command[len], BACKEND_COMMAND_MAX_SIZE-len, "-o \""SV_format"\" ", SV_v_args(args->output_filename));
        }
        snprintf(&command[len], BACKEND_COMMAND_MAX_SIZE-len, " \""SV_format"\" ", SV_v_args(args->emit_asm_path));
        run_command(command);
    } else if (args->emit_ssa_provided && args->emit_asm_provided) {
        // 1. run qbe
        // 2. run gcc
        int len = snprintf(command, BACKEND_COMMAND_MAX_SIZE, SV_format, SV_v_args(args->qbe_bin));
        if (args->qbe_target.len != 0) {
            len += snprintf(&command[len], BACKEND_COMMAND_MAX_SIZE-len, " -t "SV_format" ", SV_v_args(args->qbe_target));
        }
        len += snprintf(&command[len], BACKEND_COMMAND_MAX_SIZE-len, " \""SV_format"\" ", SV_v_args(args->emit_ssa_path));
        len += snprintf(&command[len], BACKEND_COMMAND_MAX_SIZE-len, " -o \""SV_format"\" ", SV_v_args(args->emit_asm_path));

        len += snprintf(&command[len], BACKEND_COMMAND_MAX_SIZE-len, " && ");

        len += snprintf(&command[len], BACKEND_COMMAND_MAX_SIZE-len, SV_format" -x assembler ", SV_v_args(args->cc_bin));
        if (args->cc_args.len != 0) {
            len += snprintf(&command[len], BACKEND_COMMAND_MAX_SIZE-len, SV_format" ", SV_v_args(args->cc_args));
        }
        if (args->output_provided) {
            len += snprintf(&command[len], BACKEND_COMMAND_MAX_SIZE-len, "-o \""SV_format"\" ", SV_v_args(args->output_filename));
        }
        snprintf(&command[len], BACKEND_COMMAND_MAX_SIZE-len, " \""SV_format"\" ", SV_v_args(args->emit_asm_path));
        run_command(command);
    } else if (!args->emit_ssa_provided && !args->emit_asm_provided) {
        // do nothing
    } else {
        UNREACHABLE()
    }
}