#include "args.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

static uint32_t COMPILER_VERSIONING[3] = {0,1,0};


static const char* default_cc(void);

static void print_usage(const SV program_name) {
    fprintf(stderr,
        "Usage: " SV_format " [options] <input.klep>\n"
        "\n"
        "Options:\n"
        "  -o <file>, --output=<file>  Set output file path (default: C compiler default)\n"
        "  --emit-ssa <path>           Emit QBE SSA IR to file (default: empty)\n"
        "  --emit-asm <path>           Emit assembly to file (default: empty)\n"
        "  --qbe-bin <path>            Set QBE binary path (default: qbe)\n"
        "  --cc-bin <path>             Set C compiler binary path (default: %s)\n"
        "  --qbe-target <target>       Set QBE target (default: platform default)\n"
        "  --cc-args <args>            Extra arguments passed to C compiler (default: empty)\n"
        "  -h, --help                  Show this help message and exit\n"
        "  -v, --version               Show compiler version and exit\n"
        "\n"
        "Arguments:\n"
        "  <input.klep>                The Klep source file to compile (required)\n",
        SV_v_args(program_name), default_cc()
    );
}


static void print_version(void) {
    printf("Klep Compiler - version %u.%u.%u\n", COMPILER_VERSIONING[0], COMPILER_VERSIONING[1], COMPILER_VERSIONING[2]);
}


static const char* default_cc(void) {
    const char* cc = getenv("CC");
    return (cc && *cc) ? cc : "gcc";
}


static bool match_long_opt(
    const SVList* restrict args_list,
    size_t* restrict i,
    const char* flag_name,
    const size_t flag_len,
    const SV program_name,
    SV* restrict out_val
) {
    const SV arg = args_list->array[*i];

    // --flag <val>
    if (SV_pv_cmp_eq(&arg, flag_name, flag_len)) {
        if (*i + 1 >= args_list->len) {
            fprintf(stderr, "Error: %s requires an argument.\n\n", flag_name);
            print_usage(program_name);
            exit(1);
        }
        (*i)++;
        *out_val = args_list->array[*i];
        return true;
    }

    // --flag=<val>
    if (arg.len > flag_len && arg.start[flag_len] == '=') {
        const SV prefix = SV_lrslice_from_SV(&arg, 0, flag_len);
        if (SV_pv_cmp_eq(&prefix, flag_name, flag_len)) {
            *out_val = SV_lrslice_from_SV(&arg, flag_len + 1, arg.len - (flag_len + 1));
            return true;
        }
    }

    return false;
}


CompilerArgs parse_args(const SVList* restrict args_list, const SV program_name) {
    const char* cc = default_cc();
    CompilerArgs args = (CompilerArgs){
        .qbe_bin = SV_from_str_len("qbe", 3),
        .cc_bin = SV_from_str(cc),
    };

    if (args_list->len < 2) {
        fprintf(stderr, "Error: No input file specified.\n\n");
        print_usage(program_name);
        exit(1);
    }

    for (size_t i = 1; i < args_list->len; i++) {
        SV arg = args_list->array[i];

        // --help / -h
        if (SV_pv_cmp_eq(&arg, "--help", 6) || SV_pv_cmp_eq(&arg, "-h", 2)) {
            print_usage(program_name);
            exit(0);
        }

        // --version / -v
        if (SV_pv_cmp_eq(&arg, "--version", 9) || SV_pv_cmp_eq(&arg, "-v", 2)) {
            print_version();
            exit(0);
        }

        // --output <file> / --output=<file>
        if (match_long_opt(args_list, &i, "--output", 8, program_name, &args.output_filename)) {
            args.output_provided = true;
            continue;
        }

        // -o <file>
        if (SV_pv_cmp_eq(&arg, "-o", 2)) {
            if (i + 1 >= args_list->len) {
                fprintf(stderr, "Error: -o requires an argument.\n\n");
                print_usage(program_name);
                exit(1);
            }
            i++;
            args.output_filename = args_list->array[i];
            args.output_provided = true;
            continue;
        }

        // --emit-ssa <path> / --emit-ssa=<path>
        if (match_long_opt(args_list, &i, "--emit-ssa", 10, program_name, &args.emit_ssa_path)) {
            args.emit_ssa_provided = true;
            continue;
        }

        // --emit-asm <path> / --emit-asm=<path>
        if (match_long_opt(args_list, &i, "--emit-asm", 10, program_name, &args.emit_asm_path)) {
            args.emit_asm_provided = true;
            continue;
        }

        // --qbe-bin <path> / --qbe-bin=<path>
        if (match_long_opt(args_list, &i, "--qbe-bin", 9, program_name, &args.qbe_bin)) {
            continue;
        }

        // --cc-bin <path> / --cc-bin=<path>
        if (match_long_opt(args_list, &i, "--cc-bin", 8, program_name, &args.cc_bin)) {
            continue;
        }

        // --qbe-target <target> / --qbe-target=<target>
        if (match_long_opt(args_list, &i, "--qbe-target", 12, program_name, &args.qbe_target)) {
            continue;
        }

        // --cc-args <args> / --cc-args=<args>
        if (match_long_opt(args_list, &i, "--cc-args", 9, program_name, &args.cc_args)) {
            continue;
        }

        // if it starts with '-', it's an unknown flag
        if (arg.len > 0 && arg.start[0] == '-') {
            fprintf(stderr, "Error: Unknown option '" SV_format "'\n\n", SV_v_args(arg));
            print_usage(program_name);
            exit(1);
        }

        // positional argument (input file)
        if (!args.input_provided) {
            args.input_filename = arg;
            args.raw_input_filename = arg.start;
            args.input_provided = true;
        } else {
            fprintf(stderr, "Error: Multiple input files not supported.\n\n");
            print_usage(program_name);
            exit(1);
        }
    }

    // check that input file was provided
    if (!args.input_provided) {
        fprintf(stderr, "Error: No input file specified.\n\n");
        print_usage(program_name);
        exit(1);
    }

    // // default output filename to input.out if not provided
    // if (!args.output_provided) {
    //
    //     // alloc for input_filename + ".out"
    //     // intentional memory leak, this name will be for the duration of the compilation
    //     char* output_buf = malloc(args.input_filename.len + 5);
    //     memcpy(output_buf, args.input_filename.start, args.input_filename.len);
    //     memcpy(output_buf + args.input_filename.len, ".out", 4);
    //     output_buf[args.input_filename.len + 4] = '\0';
    //
    //     args.output_filename = SV_from_str_len(output_buf, args.input_filename.len + 4);
    //     args.output_provided = true;
    // }

    return args;
}