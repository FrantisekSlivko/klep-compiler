#include "mir_to_qbe.h"

#include "mir.h"
#include "passes/type_utils.h"
#include "passes/typechecking/utils.h"


// void mir_types_to_qbe(const TC_TypeTable* restrict nct, TC_typeid_LIST* restrict out_table, FILE* restrict qbe_out) {
//
//     fprintf(qbe_out, "type :t0 = {}\n"); TC_typeid_LIST_push(out_table, (TC_typeid){.ty=TC_TY_Void});
//     fprintf(qbe_out, "type :t1 = { b }\n"); TC_typeid_LIST_push(out_table, (TC_typeid){.ty=TC_TY_Primitive, .info.primitive = TC_Primitive__Int8});
//     fprintf(qbe_out, "type :t2 = { h }\n"); TC_typeid_LIST_push(out_table, (TC_typeid){.ty=TC_TY_Primitive, .info.primitive = TC_Primitive__Int16});
//     fprintf(qbe_out, "type :t3 = { w }\n"); TC_typeid_LIST_push(out_table, (TC_typeid){.ty=TC_TY_Primitive, .info.primitive = TC_Primitive__Int32});
//     fprintf(qbe_out, "type :t4 = { l }\n"); TC_typeid_LIST_push(out_table, (TC_typeid){.ty=TC_TY_Primitive, .info.primitive = TC_Primitive__Int64});
//
//     fprintf(qbe_out, "type :t5 = { b }\n"); TC_typeid_LIST_push(out_table, (TC_typeid){.ty=TC_TY_Primitive, .info.primitive = TC_Primitive__Uint8});
//     fprintf(qbe_out, "type :t6 = { h }\n"); TC_typeid_LIST_push(out_table, (TC_typeid){.ty=TC_TY_Primitive, .info.primitive = TC_Primitive__Uint16});
//     fprintf(qbe_out, "type :t7 = { w }\n"); TC_typeid_LIST_push(out_table, (TC_typeid){.ty=TC_TY_Primitive, .info.primitive = TC_Primitive__Uint32});
//     fprintf(qbe_out, "type :t8 = { l }\n"); TC_typeid_LIST_push(out_table, (TC_typeid){.ty=TC_TY_Primitive, .info.primitive = TC_Primitive__Uint64});
//
//     fprintf(qbe_out, "type :t9 = { s }\n"); TC_typeid_LIST_push(out_table, (TC_typeid){.ty=TC_TY_Primitive, .info.primitive = TC_Primitive__Float32});
//     fprintf(qbe_out, "type :t10 = { d }\n"); TC_typeid_LIST_push(out_table, (TC_typeid){.ty=TC_TY_Primitive, .info.primitive = TC_Primitive__Float64});
//
//     fprintf(qbe_out, "type :t11 = { b }\n"); TC_typeid_LIST_push(out_table, (TC_typeid){.ty=TC_TY_Primitive, .info.primitive = TC_Primitive__Char});
//     fprintf(qbe_out, "type :t12 = { b }\n"); TC_typeid_LIST_push(out_table, (TC_typeid){.ty=TC_TY_Primitive, .info.primitive = TC_Primitive__Bool});
//
//     uint64_t id = 12 + 1 ; // TC_Primitive__Bool + 1
//
//     // for (size_t n = 0; n < nct->structs.len; n++) {
//     //     fprintf(qbe_out, "type :t%lu = { b }");
//     //     TC_typeid_LIST_push(out_table, (TC_typeid){.ty=TC_TY_Struct,.info.type_id = n});
//     // }
// }


bool is_reg_t(const TC_typeid* restrict t, uint64_t* restrict id_out, const TC_typeid_LIST* restrict qbe_mat_needing_types) {
    for (size_t n = 0; n < qbe_mat_needing_types->len; n++) {
        if (is_type_eq(t, &qbe_mat_needing_types->array[n])) {
            *id_out = n;
            return true;
        }
    }
    return false;
}

// /// deprecate
// uint64_t qbe_type_to_id(const TC_typeid* restrict t, TC_typeid_LIST* restrict qbe_mat_needing_types) {
//     uint64_t id = 0;
//     if (is_reg_t(t, &id, qbe_mat_needing_types)) {
//         return id;
//     }
//     id = qbe_mat_needing_types->len;
//     TC_typeid_LIST_push(qbe_mat_needing_types, *t);
//     return id;
// }

void write_type(const TC_typeid* restrict t, TC_typeid_LIST* restrict qbe_mat_needing_types, FILE* restrict qbe_out) {
    if (t->ty == TC_TY_Void) INTERNAL_ERROR()
    if (t->ty == TC_TY_Primitive) {
        switch (t->info.primitive) {
            case TC_Primitive__Int8: { fprintf(qbe_out, "b "); return; }
            case TC_Primitive__Int16: { fprintf(qbe_out, "h "); return; }
            case TC_Primitive__Int32: { fprintf(qbe_out, "w "); return; }
            case TC_Primitive__Int64: { fprintf(qbe_out, "l "); return; }

            case TC_Primitive__Uint8: { fprintf(qbe_out, "b "); return; }
            case TC_Primitive__Uint16: { fprintf(qbe_out, "h "); return; }
            case TC_Primitive__Uint32: { fprintf(qbe_out, "w "); return; }
            case TC_Primitive__Uint64: { fprintf(qbe_out, "l "); return; }

            case TC_Primitive__Float32: { fprintf(qbe_out, "s "); return; }
            case TC_Primitive__Float64: { fprintf(qbe_out, "d "); return; }

            case TC_Primitive__Char: { fprintf(qbe_out, "b "); return; }
            case TC_Primitive__Bool: { fprintf(qbe_out, "b "); return; }
        }
        return;
    }

    uint64_t id = 0;
    if (is_reg_t(t, &id, qbe_mat_needing_types)) {
        fprintf(qbe_out, ":t%lu ", id);
    }
    id = qbe_mat_needing_types->len;
    TC_typeid_LIST_push(qbe_mat_needing_types, *t);
    fprintf(qbe_out, ":t%lu ", id);
}


void mir_instr_to_qbe(const MIR_instr* restrict instr, FILE* restrict qbe_out, const TC_TypeTable* restrict nct, const TargetInfo* restrict target_info, TC_typeid_LIST* restrict qbe_mat_needing_types);

void mir_fn_to_qbe(const MIR_func* restrict fn, const TC_TypeTable* restrict nct, TC_typeid_LIST* restrict qbe_mat_needing_types, khash_t(sv_u64_map)* restrict identifier_map, FILE* restrict qbe_out, const TargetInfo* restrict target_info) {

    // for (size_t n = 0; n < fn->args.len; n++) {
    //     // fn->args.array[n]
    //     const SV arg_t_sv = format_TC_typeid(&fn->args.array[n], identifier_map, nct);
    //     fprintf(qbe_out, "# arg %%%lu - type '"SV_format"'\n", n+1, SV_v_args(arg_t_sv));
    // }




    if (SV_pv_cmp_eq(&fn->fn_name.span.sv, "main", 4)) {
        fprintf(qbe_out, "\n\nexport function ");

        if (fn->has_ret) {
            // const uint64_t t_id = qbe_type_to_id(&fn->ret_type, qbe_mat_needing_types);
            // fprintf(qbe_out, ":t%lu ", t_id);
            write_type(&fn->ret_type, qbe_mat_needing_types, qbe_out);
        }
        fprintf(qbe_out, " $main(");
    } else {
        fprintf(qbe_out, "\n\nfunction ");
        if (fn->has_ret) {
            // const uint64_t t_id = qbe_type_to_id(&fn->ret_type, qbe_mat_needing_types);
            // fprintf(qbe_out, ":t%lu ", t_id);
            write_type(&fn->ret_type, qbe_mat_needing_types, qbe_out);
        }
        fprintf(qbe_out, " $"SV_format"(", SV_v_args(fn->fn_raw_name));
    }


    for (size_t n = 0; n < fn->args.len; n++) {
        // const uint64_t t_id = qbe_type_to_id(&fn->args.array[n], qbe_mat_needing_types);
        // fprintf(qbe_out, ":t%lu %%v%lu", t_id, n+1);
        write_type(&fn->args.array[n], qbe_mat_needing_types, qbe_out);
        fprintf(qbe_out, " %%v%lu", n+1);
        if (n + 1 != fn->args.len) {
            fprintf(qbe_out, ", ");
        }
    }
    fprintf(qbe_out, ") {\n");

    for (size_t block_id = 0; block_id < fn->blocks.len; block_id++) {
        fprintf(qbe_out, "@block_%lu\n", block_id);
        for (size_t i = 0; i < fn->blocks.array[block_id].instructions.len; i++) {
            mir_instr_to_qbe(&fn->blocks.array[block_id].instructions.array[i], qbe_out, nct, target_info, qbe_mat_needing_types);
        }
    }

    fprintf(qbe_out, "}\n");
}

SV reg_size_to_letter(const uint64_t align) {
    switch (align) {
        case 1: {
            return (SV) {.start = "b", .len = 1};
        }
        case 2: {
            return (SV) {.start = "h", .len = 1};
        }
        case 4: {
            return (SV) {.start = "w", .len = 1};
        }
        case 8: {
            return (SV) {.start = "l", .len = 1};
        }
        default: {
            INTERNAL_ERROR()
        }
    }
}

// SV type_to_stack_val_size(const TC_typeid* restrict t, const TC_TypeTable* restrict nct, const TargetInfo* restrict target_info) {
//     static SV ptr_size;
//     static bool has_ptr_size = false;
//     if (!has_ptr_size) {
//         switch (target_info->ptr_size) {
//             case 1: {
//                 ptr_size = (SV) {.start = "b", .len = 1};
//                 break;
//             }
//             case 2: {
//                 ptr_size = (SV) {.start = "h", .len = 1};
//                 break;
//             }
//             case 4: {
//                 ptr_size = (SV) {.start = "l", .len = 1};
//                 break;
//             }
//             case 8: {
//                 ptr_size = (SV) {.start = "w", .len = 1};
//                 break;
//             }
//             default: {
//                 INTERNAL_ERROR()
//             }
//         }
//         has_ptr_size = true;
//     }
//     switch (t->ty) {
//         case TC_TY_Void: INTERNAL_ERROR()
//         case TC_TY_Primitive: {
//             switch (t->info.primitive) {
//                 case TC_Primitive__Int8: { return (SV) {.start = "sb", .len = 2}; }
//                 case TC_Primitive__Int16: { return (SV) {.start = "sh", .len = 2}; }
//                 case TC_Primitive__Int32: { return (SV) {.start = "sl", .len = 2}; }
//                 case TC_Primitive__Int64: { return (SV) {.start = "sw", .len = 2}; }
//                 case TC_Primitive__Uint8: { return (SV) {.start = "b", .len = 1}; }
//                 case TC_Primitive__Uint16: { return (SV) {.start = "h", .len = 1}; }
//                 case TC_Primitive__Uint32: { return (SV) {.start = "l", .len = 1}; }
//                 case TC_Primitive__Uint64: { return (SV) {.start = "w", .len = 1}; }
//                 case TC_Primitive__Float32: { return (SV) {.start = "s", .len = 1}; }
//                 case TC_Primitive__Float64: { return (SV) {.start = "d", .len = 1}; }
//                 case TC_Primitive__Char: { return (SV) {.start = "b", .len = 1}; }
//                 case TC_Primitive__Bool: { return (SV) {.start = "b", .len = 1}; }
//             }
//             INTERNAL_ERROR()
//         }
//         case TC_TY_Ref:
//         case TC_TY_Struct: {
//             return ptr_size;
//         }
//         case TC_TY_Enum:
//             break;
//         case TC_TY_Array: {
//             const SizeAlign sa = calc_size_of(t, nct, target_info);
//             const uint64_t s = t->info.t_array.array_size * sa.size;
//             if (s < target_info->reg_size) {}
//             break;
//         }
//         case TC_TY_List:
//             break;
//         case TC_TY_Box:
//             break;
//         case TC_TY_Slice:
//             break;
//         case TC_TY_External_Type:
//             break;
//     }
// }

void mir_instr_to_qbe(const MIR_instr* restrict instr, FILE* restrict qbe_out, const TC_TypeTable* restrict nct, const TargetInfo* restrict target_info, TC_typeid_LIST* restrict qbe_mat_needing_types) {
    switch (instr->type) {
        case MIR_TY__INVALID: INTERNAL_ERROR("MIR_TY__INVALID")
        case MIR_TY__ALLOC: {
            const SizeAlign sa = calc_size_of(&instr->info.alloc.type, nct, target_info);
            const SV val_st_sz = reg_size_to_letter(sa.align);
            fprintf(qbe_out, "  %%v%lu ="SV_format" alloc%lu %lu\n", instr->info.alloc.dest, SV_v_args(val_st_sz), sa.align, sa.size);
            break;
        }
        case MIR_TY__CONST_INT: {
            const SizeAlign sa = calc_size_of(&instr->info.const_int.type, nct, target_info);
            const SV val_st_sz = reg_size_to_letter(sa.align);
            fprintf(qbe_out, "  %%v%lu ="SV_format" copy %li\n", instr->info.const_int.dest, SV_v_args(val_st_sz), instr->info.const_int.val);
            break;
        }
        case MIR_TY__CONST_FLOAT: {
            const SizeAlign sa = calc_size_of(&instr->info.const_float.type, nct, target_info);
            SV val_st_sz;
            if (sa.size == 8) {
                val_st_sz = (SV) {.start = "l", .len = 1};
            } else {
                val_st_sz = (SV) {.start = "w", .len = 1};
            }
            fprintf(qbe_out, "  %%v%lu ="SV_format" copy %lf\n", instr->info.const_float.dest, SV_v_args(val_st_sz), instr->info.const_float.val);
            break;
        }
        case MIR_TY__CONST_STRING: {
            UNIMPLEMENTED()
        }
        case MIR_TY__MOVE: {
            const SizeAlign sa = calc_size_of(&instr->info.move.type, nct, target_info);
            SV val_st_sz;
            if (sa.size == 8) {
                val_st_sz = (SV) {.start = "l", .len = 1};
            } else {
                val_st_sz = (SV) {.start = "w", .len = 1};
            }
            fprintf(qbe_out, "  %%v%lu ="SV_format" copy %%v%lu\n", instr->info.move.dest, SV_v_args(val_st_sz), instr->info.move.src);
            break;
        }
        case MIR_TY__LOAD: {
            fprintf(qbe_out, "  # LOAD\n");
            break;
        }
        case MIR_TY__STORE: {
            fprintf(qbe_out, "  # STORE\n");
            break;
        }
        case MIR_TY__READ_PTR:
            break;
        case MIR_TY__REF_OF:
            break;
        case MIR_TY__CALC_PTR:
            break;
        case MIR_TY__STRUCT_INIT:
            break;
        case MIR_TY__MAKE_ARRAY:
            break;
        case MIR_TY__MAKE_ENUM:
            break;
        case MIR_TY__MAKE_BOX:
            break;
        case MIR_TY__CAST:
            break;
        case MIR_TY__OP_BINOP: {
            const SizeAlign sa = calc_size_of(&instr->info.binop.type, nct, target_info);
            const SV val_st_sz = reg_size_to_letter(sa.align);
            SV op;
            switch (instr->info.binop.op) {
                case MIR_BINOP_ADD: { op = (SV) {.start="add", .len=3}; break; }
                case MIR_BINOP_SUB: { op = (SV) {.start="sub", .len=3}; break; }
                case MIR_BINOP_MUL: { op = (SV) {.start="mul", .len=3}; break; }
                case MIR_BINOP_DIV: { op = (SV) {.start="div", .len=3}; break; }
                case MIR_BINOP_MOD: { op = (SV) {.start="mod", .len=3}; break; }
                case MIR_BINOP_POW: { op = (SV) {.start="pow", .len=3}; break; }

                case MIR_BINOP_BIT_AND:
                case MIR_BINOP_BIT_OR:
                case MIR_BINOP_BIT_XOR:
                case MIR_BINOP_SHL:
                case MIR_BINOP_SHR: { UNIMPLEMENTED() }
            }
            fprintf(qbe_out, "  %%v%lu ="SV_format" "SV_format" %%v%lu, %%v%lu\n", instr->info.binop.dest, SV_v_args(val_st_sz), SV_v_args(op), instr->info.binop.a, instr->info.binop.b);
            break;
        }
        case MIR_TY__OP_CMP:
            break;
        case MIR_TY__BRANCH:
            break;
        case MIR_TY__JUMP:
            break;
        case MIR_TY__CALL: {
            // instr->info.call.
            if (instr->info.call.has_dest) {
                const SizeAlign sa = calc_size_of(&instr->info.call.ret_type, nct, target_info);
                const SV val_st_sz = reg_size_to_letter(sa.align);
                fprintf(qbe_out, "  %%v%lu ="SV_format" ", instr->info.call.dest, SV_v_args(val_st_sz));
            } else {
                fprintf(qbe_out, "  ");
            }
            fprintf(qbe_out, "call $"SV_format" (", SV_v_args(instr->info.call.raw_name));
            for (size_t n = 0; n < instr->info.call.args.len; n++) {
                write_type(&instr->info.call.arg_types.array[n], qbe_mat_needing_types, qbe_out);
                // const uint64_t id = qbe_type_to_id(&instr->info.call.arg_types.array[n], qbe_mat_needing_types);
                // fprintf(qbe_out, ":t%lu %%v%lu", id, instr->info.call.args.array[n]);
                fprintf(qbe_out, " %%v%lu", instr->info.call.args.array[n]);
                if (n + 1 != instr->info.call.args.len) {
                    fprintf(qbe_out, ", ");
                }

            }
            fprintf(qbe_out, ")\n");
            break;
        }
        case MIR_TY__INDIRECT_CALL: {
            UNIMPLEMENTED()
            break;
        }
        case MIR_TY__RETURN: {
            fprintf(qbe_out, "  ret %%v%lu\n", instr->info.return_val.val);
            break;
        }
        case MIR_TY__RETURN_VOID: {
            fprintf(qbe_out, "  ret\n");
            break;
        }
        case MIR_TY__DROP:
            break;
        case MIR_TY__PHI:
            break;
    }
}