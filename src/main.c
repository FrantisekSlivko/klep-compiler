#include "main.h"
#include "tokenizer.h"

#include <stdio.h>
#include <string.h>

#include "compiler.h"
#include "passes/mir_process.h"
#include "passes/parser/parser.h"
#include "passes/typechecking/typechecking.h"



static uint32_t COMPILER_VERSIONING[3] = {0,1,0};

typedef struct {
    SV input_filename;
    const char* raw_input_filename;
    bool input_provided;
    SV output_filename;
    bool output_provided;
} CompilerArgs;


static void print_usage(const SV program_name) {
    fprintf(stderr,
        "Usage: " SV_format " [options] <input.klep>\n"
        "\n"
        "Options:\n"
        "  -o <file>, --output=<file>  Set output file path (default: <input>.out)\n"
        "  -h, --help                  Show this help message and exit\n"
        "  -v, --version               Show compiler version and exit\n"
        "\n"
        "Arguments:\n"
        "  <input.klep>                The Klep source file to compile (required)\n",
        SV_v_args(program_name)
    );
}


static void print_version(void) {
    printf("Klep Compiler - version %u.%u.%u\n", COMPILER_VERSIONING[0], COMPILER_VERSIONING[1], COMPILER_VERSIONING[2]);
}


static CompilerArgs parse_args(const SVList* restrict args_list, const SV program_name) {
    CompilerArgs args = (CompilerArgs){0};

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

        // --output=<file>
        if (arg.len > 9 && SV_pv_cmp_eq(&arg, "--output=", 9)) {
            args.output_filename = SV_lrslice_from_SV(&arg, 9, arg.len - 9);
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

    // default output filename to input.out if not provided
    if (!args.output_provided) {
        // alloc for input_filename + ".out"
        // intentional memory leak, this name will be for the duration of the compilation
        char* output_buf = malloc(args.input_filename.len + 5);
        memcpy(output_buf, args.input_filename.start, args.input_filename.len);
        memcpy(output_buf + args.input_filename.len, ".out", 4);
        output_buf[args.input_filename.len + 4] = '\0';

        args.output_filename = SV_from_str_len(output_buf, args.input_filename.len + 4);
        args.output_provided = true;
    }

    return args;
}



int main(const int argc, char* argv[]) {
    SVList args_list = SVList_new();
    for (int i = 0; i < argc; i++) {
        const size_t len = strlen(argv[i]);
        SVList_push(&args_list, (SV){argv[i], len});
    }

    const SV program_name = args_list.array[0];
    const CompilerArgs args = parse_args(&args_list, program_name);

    SVList_free(&args_list);

    const char* raw_source_filename = args.raw_input_filename;
    const SV source_filename = args.input_filename;

    // printf("raw_source_filename = '%s'\n", raw_source_filename);

    FILE* source_file = fopen(raw_source_filename, "rb");
    if (source_file == NULL) {
        perror("Failed to open file");
        exit(1);
    }


    // Seek to the end of the file
    if (fseek(source_file, 0, SEEK_END) != 0) {
        fclose(source_file);
        fprintf(stderr, "Failed file read\n");
        exit(1);
    }

    const long source_size = ftell(source_file);

    if (fseek(source_file, 0, SEEK_SET) != 0) {
        fclose(source_file);
        fprintf(stderr, "Failed file read\n");
        exit(1);
    }

    char* raw_source = malloc(source_size+1);
    fread(raw_source, 1, source_size, source_file);
    raw_source[source_size] = '\0';

    fclose(source_file);

    const SV source = SV_from_str_len(raw_source, source_size);

    printf("[info] file loaded\n");

    const TargetInfo target_info = (TargetInfo) {
        8,
        8,
        8
    };

    const SourceInfo source_info = (SourceInfo) {
        .source = source,
        .source_filename = source_filename
    };

    main_compile(target_info, source_info);

    free(raw_source);
    return 0;
}

