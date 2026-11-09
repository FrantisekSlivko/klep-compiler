
#include "mir_process.h"

#include "passes/typechecking/lowering.h"
#include "passes/typeinference/typeinference.h"
#include "passes/variable_storage_promotion/variable_storage_promotion.h"


MIR_func TC_process_fn(
    const IR_func fn,
    khash_t(sv_u64_map)* restrict identifier_map,
    const IR_func_LIST* restrict functions,
    const CFFI_IR_extern_fn_LIST* restrict external_functions,
    TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info,
    TC_typeid_LIST* restrict type_needing_destructor
) {
    {
        const SV fn_name = fn.fn_name.span.sv;
        printf("TYPECHECKING FN '"SV_format"' {raw='"SV_format"'}\n", SV_v_args(fn_name), SV_v_args(fn.raw_name));
    }

    TC_Entry_LIST temporals;

    type_inference_pass_over_fn(fn, identifier_map, functions, external_functions, nct, target_info, source_info,
                                &temporals);

    for (size_t t = 1; t < temporals.len; t++) {
        const TC_Entry e = temporals.array[t];
        if (!e.known_temporal) {
            printf("  %%%lu UNKNOWN\n", t);
            continue;
        }
        if (e.type.ty == TC_TY_Void) {
            printf("ERROR: VOID TYPE %%%lu\n", t);
            continue;
        }
        const SV e_t_sv = format_TC_typeid(&e.type, identifier_map, nct);
        printf("   %%%lu = {"SV_format"}\n", t, SV_v_args(e_t_sv));
    }

    /// determine if variable is no longer a pure temporal, but ALLOC->PTR
    IR_temporal_LIST promote_to_stack_temps = IR_temporal_LIST_new();

    variable_storage_promotion_pass(fn, identifier_map, temporals, &promote_to_stack_temps, nct, target_info);

    printf("------- promote_to_stack_temps ---- \n");
    for(size_t n = 0; n < promote_to_stack_temps.len; n++) {
        printf("  %%%lu\n", promote_to_stack_temps.array[n].val);
    }

//     UNIMPLEMENTED("variable storage promotion analys pass");

    // todo: pass `promote_to_stack_temps` into lowering_and_emission_pass_over_fn
    const MIR_func mir_fn = lowering_and_emission_pass_over_fn(
        fn,
        identifier_map,
        functions,
        external_functions,
        nct, target_info, source_info,
        &temporals,
        promote_to_stack_temps,
        type_needing_destructor
    );

    TC_Entry_LIST_free(&temporals);
    return mir_fn;
}



/// typechecking pass (two passes) + lowering to MIR
/// performs Bidirectional Typechecking
void TC_typecheck_and_lower(
    khash_t(sv_u64_map)* restrict identifier_map,
    const IR_func_LIST* restrict functions,
    const CFFI_IR_extern_fn_LIST* restrict external_functions,
    TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info,
    TC_typeid_LIST* restrict type_needing_destructor
) {
    printf("(fist type is from type_id, second from type's span)\n");
    for (size_t n = 0; n < functions->len; n++) {
        const IR_func fn = functions->array[n];
        TC_process_fn(fn, identifier_map,
                      functions,
                      external_functions,
                      nct,
                      target_info,
                      source_info,
                      type_needing_destructor
        );
    }
    for (size_t n = 0; n < nct->materialized_generic_functions.len; n++) {
        const IR_func fn = nct->materialized_generic_functions.array[n].fn;
        TC_process_fn(fn, identifier_map,
                      functions,
                      external_functions,
                      nct,
                      target_info,
                      source_info,
                      type_needing_destructor
        );
    }
}
