#include "main.h"
#include "tokenizer.h"

#include <stdio.h>
#include <string.h>

#include "passes/mir_process.h"
#include "passes/parser/parser.h"
#include "passes/typechecking/typechecking.h"


void compile(const TargetInfo target_info, const SourceInfo source_info);

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
    // SVList list = SVList_new();
    // for (int i=0; i<argc ; i++) {
    //     const size_t len = strlen(argv[i]);
    //     SVList_push(&list, (SV) {argv[i], len});
    // }
    //
    // for (SVListView list_v = SVList_to_view(&list); SVListView_has_next(&list_v) ;) {
    //     const SV* item = SVListView_next(&list_v);
    //     printf("arg: '"SV_format"'\n", SV_p_args(item));
    // }
    //
    // SVList_free(&list);
    //
    // const char* raw_source_filename = "programs/complex_types.klep";
    // const SV source_filename = SV_from_str(raw_source_filename);
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

    compile(target_info, source_info);

    free(raw_source);
    return 0;
}

void compile(const TargetInfo target_info, const SourceInfo source_info) {
    SVList tokens = p_tokenize(source_info.source);

    // ---------------------------------------------------------------------------------

    khash_t(sv_u64_map)* identifier_map = kh_init(sv_u64_map);
    IR_func_LIST functions;
    CFFI_IR_extern_fn_LIST external_functions;
    TC_TypeTable nct = parse(&tokens, source_info, target_info, identifier_map, &functions, &external_functions);

    SVList_free(&tokens);

    // ---------------------------------------------------------------------------------
    // if (external_functions.len > 0) {
    //     printf("EXTERNAL FUNCTIONS:\n");
    //     for (size_t f = 0; f < external_functions.len; f++) {
    //         const CFFI_IR_extern_fn fn = external_functions.array[f];
    //         printf("  EXTERN ");
    //         UNIMPLEMENTED()
    //     }
    // }

    if (external_functions.len > 0) {
        printf("EXTERNAL FUNCTIONS COUNT = %lu\n", external_functions.len);
    }

    if (functions.len > 0) {
        printf("FUNCTIONS:\n");
        for (size_t f = 0; f < functions.len; f++) {
            const SpannedIdentifier fn_name = functions.array[f].fn_name;

            const SV real_fn_name = fn_name.span.sv; // SV_from_identifiermap_lookup(identifier_map, fn_name);

            printf("  FUNC <"SV_format"> -> ", SV_v_args(real_fn_name));
            print_IR_typeuse(&functions.array[f].fn_ret_t, identifier_map);
            printf(" {\n");

            for (size_t b = 0; b < functions.array[f].blocks.len; b++) {
                const IR_block* block = &functions.array[f].blocks.array[b];
                printf("    block %lu {\n", b);
                printf("      binds: { ");
                for (size_t n = 0; n < block->bindings.len; n++) {
                    const IR_SSA_binding bind = block->bindings.array[n];
                    const SV var = bind.variable.span.sv; // SV_from_identifiermap_lookup(identifier_map, bind.variable);
                    printf("'"SV_format"' %%%lu", SV_v_args(var), bind.bind.val);
                    if (n + 1 != block->bindings.len) {
                        printf(", ");
                    }
                }
                printf(" }\n");


                for (size_t i = 0; i < block->instructions.len; i++) {
                    printf("      ");
                    print_IR_instr(&block->instructions.array[i], identifier_map);
                    printf("\n");
                }
                printf("    }\n");
            }
            printf("  }\n");
        }
    }

    if (nct.structs.len > 0) {
        printf("STRUCTS:\n");
        for (size_t n=0; n<nct.structs.len; n++) {
            TC_structdef sd = nct.structs.array[n];
            TC_struct_layout sl = sd.layout;

            SV name = sd.name.span.sv;
            printf("  STRUCT '"SV_format"'{%lu}, align=%u, size=%u {\n", SV_v_args(name), n, sl.align, sl.size);
            for (size_t k=0; k<sd.fields.len; k++) {
                uint32_t offset = sl.field_offsets == (void*)nullptr ? 0 : sl.field_offsets[k];
                TC_field f = sd.fields.array[k];

                SV f_name = f.name.span.sv;

                printf("    FIELD '"SV_format"', type='", SV_v_args(f_name));
                print_TC_typeid(&f.type, identifier_map, &nct);
                printf("', offset=%u\n", offset);
            }
            printf("  }\n");

        }
    }

    if (nct.enums.len > 0) {
        printf("ENUMS:\n");
        for (size_t n=0; n<nct.enums.len; n++) {
            TC_enumdef ed = nct.enums.array[n];
            printf("  ENUM '"SV_format"'{%lu}, align=%u, size=%u {\n", SV_v_args(ed.name.span.sv), n, ed.align, ed.size);
            for (size_t v=0; v<ed.variants.len; v++) {
                TC_enumdef_variant var = ed.variants.array[v];
                printf("    "SV_format, SV_v_args(var.name.span.sv));
                if (var.values_len > 0) {
                    printf("(");
                    for (size_t l=0; l<var.values_len; l++) {
                        print_TC_typeid(&var.values[l], identifier_map, &nct);
                        if (l+1 != var.values_len) {
                            printf(", ");
                        }
                    }
                    printf(")");
                }
                if (v + 1 != ed.variants.len)
                    printf(",");
                printf("\n");
            }
            printf("  }\n");
        }
    }




    printf("COMMENCING TYPECHECKING...\n");

    TC_typeid_LIST type_needing_destructor = TC_typeid_LIST_new();
    TC_typecheck_and_lower(identifier_map, &functions, &external_functions, &nct, &target_info, &source_info, &type_needing_destructor);

    printf("DONE\n");

    if (type_needing_destructor.len > 0) {
        printf("TYPES NEEDING DESTRUCTORS:\n");
        for (size_t n = 0; n < type_needing_destructor.len; ++n) {
            printf("  DESTRUCTOR{");
            print_TC_typeid(&type_needing_destructor.array[n], identifier_map, &nct);
            printf("}\n");
        }
    }


}
