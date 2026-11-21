#include "compiler.h"

#include "ir.h"
#include "khash.h"
#include "sv_u64_map.h"
#include "tokenizer.h"
#include "typecompletion.h"
#include "cffi/wrapper_creator.h"
#include "overloading/resolve.h"
#include "passes/mir_process.h"
#include "passes/parser/parser.h"
#include "passes/qbe/mir_to_qbe.h"

Parser initial_parsing(const SourceInfo source_info, GlobalCompilerState* restrict global_compiler_state) {
    SVList tokens = p_tokenize(source_info.source);

    // IR_func_LIST functions;
    // CFFI_IR_extern_fn_LIST external_functions;

    const Parser p = parse(&tokens, source_info, global_compiler_state);

    return p;

    /*

    const TC_TypeTable nct = close_type_table(&p.type_table, p.identifier_map, p.generic_functions, &target_info, &source_info);

    *functions = p.functions;
    *external_functions = p.external_functions;

     */



    SVList_free(&tokens);
}



void main_compile(const TargetInfo target_info, const SourceInfo source_info) {

    khash_t(sv_u64_map)* identifier_map = kh_init(sv_u64_map);
    khash_t(sv_u64_map)* path_map = kh_init(sv_u64_map);

    GlobalCompilerState global_compiler_state = (GlobalCompilerState) {
        .target_info = target_info,
        .identifier_map = identifier_map,
        .identifier_map_cnt = 0,
        .absolute_file_path_map = path_map
    };

    Parser main_p = initial_parsing(source_info, &global_compiler_state);

    // for(size_t n = 0; n < main_p.type_table.imports.len; n++) {
    // }

    TC_TypeTable nct = close_type_table(&main_p.type_table, global_compiler_state.identifier_map, main_p.generic_functions, &target_info, &source_info);






    #ifndef NDEBUG
        if (main_p.external_functions.len > 0) {
            printf("EXTERNAL FUNCTIONS COUNT = %lu\n", main_p.external_functions.len);
        }

        if (main_p.functions.len > 0) {
            printf("FUNCTIONS:\n");
            for (size_t f = 0; f < main_p.functions.len; f++) {
                const SpannedIdentifier fn_name = main_p.functions.array[f].fn_name;

                const SV real_fn_name = fn_name.span.sv; // SV_from_identifiermap_lookup(identifier_map, fn_name);

                printf("  FUNC <"SV_format"> -> ", SV_v_args(real_fn_name));
                print_IR_typeuse(&main_p.functions.array[f].fn_ret_t, identifier_map);
                printf(" {\n");

                for (size_t b = 0; b < main_p.functions.array[f].blocks.len; b++) {
                    const IR_block* block = &main_p.functions.array[f].blocks.array[b];
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
    #endif


    const size_t functions_org_len = main_p.functions.len;

    // add external fn's stubs
    for (size_t n = 0; n < main_p.external_functions.len; n++) {
        const CFFI_IR_extern_fn ef = main_p.external_functions.array[n];
        const SV mangled_name = mangle_function_name(&ef.kelp_fn_name, 0);
        // const Identifier mangled_name_identifier = P_to_identifier(&main_p, &mangled_name);

        IR_typename_pair_LIST args = IR_typename_pair_LIST_new_with_capacity(ef.args.len);
        for (size_t a = 0; a < ef.args.len; a++) {
            IR_typename_pair_LIST_push(&args, (IR_typename_pair) {
                .type = ef.args.array[a].type,
                .name = ef.args.array[a].name
            });
        }
        IR_func_LIST_push(&main_p.functions, (IR_func) {
            .fn_ret_t = ef.ret.type,
            .fn_name = ef.kelp_fn_name,
            .raw_name = mangled_name,
            .args = args,
            .blocks = {0},
            .highest_temporal_number = 0,
            .error_info_list = {0}
        });
        // printf("added external fn to main_p.function : name='"SV_format"'\n", SV_v_args(ef.kelp_fn_name.span.sv));
    }



    TC_typeid_LIST type_needing_destructor = TC_typeid_LIST_new();
    const MIR_func_LIST mir_funcs = TC_typecheck_and_lower(identifier_map, &main_p.functions, &nct, &target_info, &source_info, &type_needing_destructor);

    // external functions map to after functions, and before materialized functions
    for (size_t f = 0; f < main_p.external_functions.len; f++) {
        const size_t i = f + functions_org_len;
        const CFFI_IR_extern_fn fn = main_p.external_functions.array[f];

        const MIR_func mir_func = create_wrap_cffi_fn(fn, &nct, &target_info, &source_info, identifier_map);
        mir_funcs.array[i] = mir_func;
    }

    FILE* qbe_out = fopen("out.ssa", "w+");
    // FILE* qbe_type_out = fopen("out_type.ssa", "w+");

    if (qbe_out == NULL) {
        perror("fopen");
    }
    // if (qbe_type_out == NULL) {
        // perror("fopen");
    // }

    TC_typeid_LIST qbe_type_table = TC_typeid_LIST_new();
    SVList global_string_const_table = SVList_new();

    mir_types_preregister(&nct, &qbe_type_table, identifier_map, qbe_out, &target_info);

    for (size_t n = 0; n < mir_funcs.len; n++) {
        mir_fn_to_qbe(&mir_funcs.array[n], &nct, &qbe_type_table, identifier_map, qbe_out, &target_info, &global_string_const_table);
    }

    // mir_emit_type_table(&qbe_type_table, &nct, qbe_type_out, &target_info, identifier_map);
    fprintf(qbe_out, "\n\n");
    for (size_t n = 0; n < global_string_const_table.len; n++) {
        fprintf(qbe_out, "data $str%lu = { b ", n);
        for (size_t c = 0; c < global_string_const_table.array[n].len; c++) {
            fprintf(qbe_out, "%u ", global_string_const_table.array[n].start[c]);
        }
        fprintf(qbe_out, ", b 0 }\n");
    }

    // fclose(qbe_type_out);
    fclose(qbe_out);

    printf("DONE\n");

    // ---------------------------------------------------------------------------------

    // ---------------------------------------------------------------------------------
    // if (external_functions.len > 0) {
    //     printf("EXTERNAL FUNCTIONS:\n");
    //     for (size_t f = 0; f < external_functions.len; f++) {
    //         const CFFI_IR_extern_fn fn = external_functions.array[f];
    //         printf("  EXTERN ");
    //         UNIMPLEMENTED()
    //     }
    // }


    // printf("COMMENCING TYPECHECKING...\n");
    //
    // TC_typeid_LIST type_needing_destructor = TC_typeid_LIST_new();
    // TC_typecheck_and_lower(identifier_map, &functions, &external_functions, &nct, &target_info, &source_info, &type_needing_destructor);
    //
    // printf("DONE\n");
    //
    // if (type_needing_destructor.len > 0) {
    //     printf("TYPES NEEDING DESTRUCTORS:\n");
    //     for (size_t n = 0; n < type_needing_destructor.len; ++n) {
    //         printf("  DESTRUCTOR{");
    //         print_TC_typeid(&type_needing_destructor.array[n], identifier_map, &nct);
    //         printf("}\n");
    //     }
    // }


}

