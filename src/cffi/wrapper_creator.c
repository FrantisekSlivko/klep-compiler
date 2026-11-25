#include "wrapper_creator.h"

#include "overloading/resolve.h"
#include "typeresolve/type_resolve.h"

void translate_in_arg(const CFFI_c_fn_arg c_arg,
    MIR_temporal_id* restrict out,
    TC_typeid* restrict out_tt,
    MIR_instr_LIST* restrict instrs,
    const CFFI_arg_LIST* args,
    const TC_typeid_LIST* tc_typeids,
    size_t* highest_temporal_number,
    const TargetInfo* target_info
) {
    const CFFI_c_typeuse ct = c_arg.c_type;
    switch (ct.kind) {
        case CFFI_TY_INVALID: {
            INTERNAL_ERROR()
        }
        case CFFI_TY_PRIMITIVE: {
            *out_tt = (TC_typeid) {
                .ty = TC_TY_Primitive,
                .info.primitive = ct.primitive
            };
            break;
        }
        case CFFI_TY_PTR:
        case CFFI_TY_ARRAY: {
            *out_tt = (TC_typeid){ .ty = TC_TY_Ref };
            break;
        }
        case CFFI_TY_VOID: INTERNAL_ERROR()
        case CFFI_TY_NAMED: {
            // to be made into better error later
            INTERNAL_ERROR("unknown type")
        }
    }
    bool found = false;
    for (size_t k = 0; k < args->len; k++) {
        const CFFI_arg k_arg = args->array[k];
        if (!k_arg.has_constructor && k_arg.name.identifier == c_arg.arg_name.identifier) {
            *out = k + 1;
            found = true;
            break;
        } else {

        }
        // for (size_t m = 0; m < k_arg.constructor.len; m++) {
        //     if (k_arg.has_constructor && k_arg.constructor.array[m].identifier == c_arg.arg_name.identifier) {
        //         mir_emit(instrs, (MIR_instr) {
        //             .type = MIR_TY__CALC_PTR,
        //             .info.calc_ptr = {
        //                 .dest = ,
        //                 .base = ,
        //                 .offset = ,
        //                 .scale = ,
        //                 .index = ,
        //                 .has_index =
        //             }
        //         });
        //         // , emit MIR_TY__CALC_PTR (offset 0 for m == 0, target_info->ptr_size for m == 1) and MIR_TY__READ_PTR into a fresh temporal and set *out to that temporal.)
        //     }
        // }
    }
    if (!found) {
        UNIMPLEMENTED()
    }

}

MIR_func create_wrap_cffi_fn(const CFFI_IR_extern_fn fn, TC_TypeTable* restrict nct, const TargetInfo* restrict target_info, const SourceInfo* restrict source_info, const khash_t(sv_u64_map)* restrict identifier_map) {
    MIR_block_LIST blocks = MIR_block_LIST_new();
    MIR_temporal_LIST temporals = MIR_temporal_LIST_new();
    TC_typeid_LIST args = TC_typeid_LIST_new();

    for (size_t a = 0; a < fn.args.len; a++) {
        TC_typeid_LIST_push(&args, resolve_tc_type(&fn.args.array[a].type, nct, target_info, source_info, identifier_map));
    }
    size_t highest_temporal_number = fn.args.len + 1;

    {
        MIR_instr_LIST instrs = MIR_instr_LIST_new();

        TC_typeid_LIST call_args_types = TC_typeid_LIST_new_with_capacity(fn.c_args.len);
        MIR_temporal_id_LIST call_args = MIR_temporal_id_LIST_new_with_capacity(fn.c_args.len);

        for (size_t a = 0; a < fn.c_args.len; a++) {
            MIR_temporal_id out;
            TC_typeid out_tt;
            translate_in_arg(fn.c_args.array[a], &out, &out_tt, &instrs, &fn.args, &args, &highest_temporal_number, target_info);
            MIR_temporal_id_LIST_push(&call_args, out);
            TC_typeid_LIST_push(&call_args_types, out_tt);
        }

        const uint64_t dest = highest_temporal_number++;
        mir_emit(&instrs, (MIR_instr) {
            .type = MIR_TY__CALL,
            .info.call = {
                .dest = dest,
                .has_dest = fn.ret.type.tt_ty != IR_TY_Void,
                .fn_name = fn.c_fn_name,
                .raw_name = fn.c_fn_name.span.sv,
                .args = call_args,
                .arg_types = call_args_types,
                .ret_type = {0}
            }
        });

        mir_emit(&instrs, (MIR_instr) {
            .type = MIR_TY__RETURN_VOID
        });


        MIR_block_LIST_push(&blocks, (MIR_block) {
            instrs
        });
    }

    return (MIR_func) {
        .fn_name = fn.kelp_fn_name,
        .fn_raw_name = mangle_function_name(&fn.kelp_fn_name, 0),
        .blocks = blocks,
        .highest_temporal_number = highest_temporal_number,
        .temporals = temporals,
        .args = args,
        .has_ret = fn.ret.type.tt_ty != IR_TY_Void,
        .ret_type = resolve_tc_type(&fn.ret.type, nct, target_info, source_info, identifier_map)
    };
}
