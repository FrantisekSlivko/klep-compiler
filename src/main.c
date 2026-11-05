#include "main.h"
#include "tokenizer.h"

#include <stdio.h>
#include <string.h>

#include "passes/mir_process.h"
#include "passes/parser/parser.h"
#include "passes/typechecking/typechecking.h"


void compile(SV source, SV source_filename);

int main(const int argc, char* argv[]) {
    SVList list = SVList_new();
    for (int i=0; i<argc ; i++) {
        const size_t len = strlen(argv[i]);
        SVList_push(&list, (SV) {argv[i], len});
    }

    for (SVListView list_v = SVList_to_view(&list); SVListView_has_next(&list_v) ;) {
        const SV* item = SVListView_next(&list_v);
        printf("arg: '"SV_format"'\n", SV_p_args(item));
    }

    SVList_free(&list);

    const char* raw_source_filename = "programs/i.klep";
    const SV source_filename = SV_from_str(raw_source_filename);

    FILE* source_file = fopen(raw_source_filename, "rb");
    if (source_file == NULL) {
        perror("Failed to open file");
        exit(1);
    }


    // Seek to the end of the file
    if (fseek(source_file, 0, SEEK_END) != 0) {
        fclose(source_file);
        fprintf(stderr, "failed file read\n");
        exit(1);
    }

    const long source_size = ftell(source_file);

    if (fseek(source_file, 0, SEEK_SET) != 0) {
        fclose(source_file);
        fprintf(stderr, "failed file read\n");
        exit(1);
    }

    char* raw_source = malloc(source_size+1);
    fread(raw_source, 1, source_size, source_file);
    raw_source[source_size] = '\0';

    fclose(source_file);

    // printf("source:\n");
    // printf("%s", raw_source);
    // printf("\n\n");

    const SV test_source = SV_from_str_len(raw_source, source_size);

    printf("[info] file loaded\n");

    compile(test_source, source_filename);

    free(raw_source);
    return 0;
}

void compile(const SV source, const SV source_filename) {
    SVList tokens = p_tokenize(source);

    const TargetInfo target_info = (TargetInfo) {
        4,
        4,
        8
    };

    const SourceInfo source_info = (SourceInfo) {
        .source = source,
        .source_filename = source_filename
    };

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
