#include "mir_to_qbe.h"

#include "mir.h"
#include "utils.h"
#include "passes/type_utils.h"
#include "passes/typechecking/utils.h"


// todo: question, when Slice<> List<> are passed by value, what happens?

bool is_reg_t(const TC_typeid* restrict t, uint64_t* restrict id_out, const TC_typeid_LIST* restrict qbe_mat_needing_types) {
    for (size_t n = 0; n < qbe_mat_needing_types->len; n++) {
        if (is_type_eq(t, &qbe_mat_needing_types->array[n])) {
            *id_out = n;
            return true;
        }
    }
    return false;
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


void write_type(const TC_typeid* restrict t, TC_typeid_LIST* restrict qbe_mat_needing_types, FILE* restrict qbe_out, const TargetInfo* restrict target_info) {
    if (t->ty == TC_TY_Void) INTERNAL_ERROR()
    if (t->ty == TC_TY_Primitive) {
        switch (t->info.primitive) {
            case TC_Primitive__Int8: { fprintf(qbe_out, "sb "); return; }
            case TC_Primitive__Int16: { fprintf(qbe_out, "sh "); return; }
            case TC_Primitive__Int32: { fprintf(qbe_out, "w "); return; }
            case TC_Primitive__Int64: { fprintf(qbe_out, "l "); return; }

            case TC_Primitive__Uint8: { fprintf(qbe_out, "ub "); return; }
            case TC_Primitive__Uint16: { fprintf(qbe_out, "uh "); return; }
            case TC_Primitive__Uint32: { fprintf(qbe_out, "w "); return; }
            case TC_Primitive__Uint64: { fprintf(qbe_out, "l "); return; }

            case TC_Primitive__Float32: { fprintf(qbe_out, "s "); return; }
            case TC_Primitive__Float64: { fprintf(qbe_out, "d "); return; }

            case TC_Primitive__Char: { fprintf(qbe_out, "ub "); return; }
            case TC_Primitive__Bool: { fprintf(qbe_out, "ub "); return; }
        }
        return;
    }

    if (t->ty == TC_TY_Ref || t->ty == TC_TY_Slice) {
        const SV p = reg_size_to_letter(target_info->ptr_size);
        fprintf(qbe_out, SV_format" ", SV_v_args(p));
        return;
    }


    uint64_t id = 0;
    if (is_reg_t(t, &id, qbe_mat_needing_types)) {
        fprintf(qbe_out, ":t%lu ", id);
        return;
    }
    INTERNAL_ERROR()
    // id = qbe_mat_needing_types->len;
    // TC_typeid_LIST_push(qbe_mat_needing_types, *t);
    // fprintf(qbe_out, ":t%lu ", id);
}

void mir_type_prereg(const TC_typeid* restrict t, const TC_TypeTable* restrict nct, TC_typeid_LIST* restrict qbe_mat_needing_types, khash_t(sv_u64_map)* restrict identifier_map, FILE* restrict qbe_out, const TargetInfo* restrict target_info) {
    const SizeAlign sa = calc_size_of(t, nct, target_info);
    fprintf(qbe_out, "type :t%lu = align %lu { b %lu }", qbe_mat_needing_types->len, sa.align, sa.size);
    TC_typeid_LIST_push(qbe_mat_needing_types, *t);
}

void mir_types_preregister(const TC_TypeTable* restrict nct, TC_typeid_LIST* restrict qbe_mat_needing_types, khash_t(sv_u64_map)* restrict identifier_map, FILE* restrict qbe_out, const TargetInfo* restrict target_info) {
    for (size_t n = 0; n < nct->structs.len; n++) {
        const TC_typeid t = (TC_typeid) {
            .ty = TC_TY_Struct,
            .info.type_id = n
        };
        mir_type_prereg(&t, nct, qbe_mat_needing_types, identifier_map, qbe_out, target_info);
    }
    for (size_t n = 0; n < nct->enums.len; n++) {
        const TC_typeid t = (TC_typeid) {
            .ty = TC_TY_Enum,
            .info.type_id = n
        };
        mir_type_prereg(&t, nct, qbe_mat_needing_types, identifier_map, qbe_out, target_info);
    }
}

void mir_instr_to_qbe(const MIR_instr* restrict instr, FILE* restrict qbe_out, const TC_TypeTable* restrict nct, const TargetInfo* restrict target_info, TC_typeid_LIST* restrict qbe_mat_needing_types, SVList* restrict string_const_data);

void mir_fn_to_qbe(const MIR_func* restrict fn, const TC_TypeTable* restrict nct, TC_typeid_LIST* restrict qbe_mat_needing_types, khash_t(sv_u64_map)* restrict identifier_map, FILE* restrict qbe_out, const TargetInfo* restrict target_info, SVList* restrict global_string_const_table) {


    if (SV_pv_cmp_eq(&fn->fn_name.span.sv, "main", 4)) {
        fprintf(qbe_out, "\n\nexport function ");

        if (fn->has_ret) {
            write_type(&fn->ret_type, qbe_mat_needing_types, qbe_out, target_info);
        }
        fprintf(qbe_out, "$main(");
    } else {
        fprintf(qbe_out, "\n\nfunction ");
        if (fn->has_ret) {
            write_type(&fn->ret_type, qbe_mat_needing_types, qbe_out, target_info);

        }
        fprintf(qbe_out, "$"SV_format"(", SV_v_args(fn->fn_raw_name));
    }


    for (size_t n = 0; n < fn->args.len; n++) {
        write_type(&fn->args.array[n], qbe_mat_needing_types, qbe_out, target_info);
        fprintf(qbe_out, "%%v%lu", n+1);
        if (n + 1 != fn->args.len) {
            fprintf(qbe_out, ", ");
        }
    }
    fprintf(qbe_out, ") {\n");

    for (size_t block_id = 0; block_id < fn->blocks.len; block_id++) {
        fprintf(qbe_out, "@block_%lu\n", block_id);
        for (size_t i = 0; i < fn->blocks.array[block_id].instructions.len; i++) {
            mir_instr_to_qbe(&fn->blocks.array[block_id].instructions.array[i], qbe_out, nct, target_info, qbe_mat_needing_types, global_string_const_table);
        }
    }

    fprintf(qbe_out, "}\n");
}




void mir_instr_to_qbe(const MIR_instr* restrict instr, FILE* restrict qbe_out, const TC_TypeTable* restrict nct, const TargetInfo* restrict target_info, TC_typeid_LIST* restrict qbe_mat_needing_types, SVList* restrict string_const_data) {
    switch (instr->type) {
        case MIR_TY__INVALID: INTERNAL_ERROR("MIR_TY__INVALID")
        case MIR_TY__ALLOC: {
            const SizeAlign sa = calc_size_of(&instr->info.alloc.type, nct, target_info);
            const SV val_st_sz = reg_size_to_letter(target_info->ptr_size);
            fprintf(qbe_out, "  %%v%lu ="SV_format" alloc%lu %lu\n", instr->info.alloc.dest, SV_v_args(val_st_sz), sa.align, sa.size);
            break;
        }
        case MIR_TY__CONST_INT: {
            const SizeAlign sa = calc_size_of(&instr->info.const_int.type, nct, target_info);
            const SV val_st_sz = reg_size_to_letter(sa.size);
            fprintf(qbe_out, "  %%v%lu ="SV_format" copy %li\n", instr->info.const_int.dest, SV_v_args(val_st_sz), instr->info.const_int.val);
            break;
        }
        case MIR_TY__CONST_FLOAT: {
            const SizeAlign sa = calc_size_of(&instr->info.const_float.type, nct, target_info);
            SV val_st_sz;
            if (sa.size == 8) {
                val_st_sz = (SV) {.start = "d", .len = 1};
            } else {
                val_st_sz = (SV) {.start = "s", .len = 1};
            }
            fprintf(qbe_out, "  %%v%lu ="SV_format" copy %lf\n", instr->info.const_float.dest, SV_v_args(val_st_sz), instr->info.const_float.val);
            break;
        }
        case MIR_TY__CONST_STRING: {
            const SV ptr_size = reg_size_to_letter(target_info->ptr_size);
            fprintf(qbe_out, "  %%v%lu ="SV_format" copy $str%lu\n", instr->info.const_string.dest, SV_v_args(ptr_size), string_const_data->len);
            SVList_push(string_const_data, instr->info.const_string.str_val);
            break;
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
            const SizeAlign sa = calc_size_of(&instr->info.store.val_type, nct, target_info);
            if (instr->info.store.is_val_stack) {
                fprintf(qbe_out, "  blit %%v%lu, %%v%lu, %lu\n", instr->info.store.val, instr->info.store.ptr, sa.size);
            } else {
                const SV rs = reg_size_to_letter(sa.align);
                fprintf(qbe_out, "  store"SV_format" %%v%lu, %%v%lu\n", SV_v_args(rs), instr->info.store.val, instr->info.store.ptr);
            }

            break;
        }
        case MIR_TY__READ_PTR: {
            // load or blit

            if (instr->info.read_ptr.read_size > 8) {
                fprintf(qbe_out, "  blit %%v%lu, %%si%luoff%lu, %lu\n", instr->info.read_ptr.dest, instr->info.read_ptr.ptr, instr->info.read_ptr.dest, instr->info.read_ptr.read_size);
            } else {
                const SV rs = reg_size_to_letter(instr->info.read_ptr.read_size);
                fprintf(qbe_out, "  %%v%lu =%c load"SV_format" %%v%lu\n", instr->info.read_ptr.dest, instr->info.read_ptr.read_size == 8 ? 'l' : 'w',  SV_v_args(rs), instr->info.read_ptr.ptr);
            }
            break;
        }
        case MIR_TY__REF_OF: {
            // already stack slot -> just copy
            fprintf(qbe_out, "  %%v%lu =l copy %%v%lu\n", instr->info.ref_of.dest, instr->info.ref_of.src);
            break;
        }
        case MIR_TY__CALC_PTR: {
            if (instr->info.calc_ptr.has_index) {
                fprintf(qbe_out, "  %%ccp%luc0 =l extsw %%v%lu\n", instr->info.calc_ptr.dest, instr->info.calc_ptr.index);
                fprintf(qbe_out, "  %%ccp%luc1 =l mul %%ccp%luc0, %lu\n", instr->info.calc_ptr.dest, instr->info.calc_ptr.dest, instr->info.calc_ptr.scale);
                fprintf(qbe_out, "  %%v%lu =l add %%v%lu, %lu\n", instr->info.calc_ptr.dest, instr->info.calc_ptr.base, instr->info.calc_ptr.offset);
            } else if (instr->info.calc_ptr.offset == 0) {
                fprintf(qbe_out, "  %%v%lu =l copy %%v%lu\n", instr->info.calc_ptr.dest, instr->info.calc_ptr.base);
            } else {
                fprintf(qbe_out, "  %%v%lu =l add %%v%lu, %lu\n", instr->info.calc_ptr.dest, instr->info.calc_ptr.base, instr->info.calc_ptr.offset);
            }
            break;
        }
        case MIR_TY__STRUCT_INIT: {

            const TC_structdef* sd = &nct->structs.array[instr->info.struct_init.struct_type_id];

            const SV rg = reg_size_to_letter(target_info->ptr_size);
            fprintf(qbe_out, "  %%v%lu ="SV_format" alloc%u %u\n", instr->info.struct_init.dest, SV_v_args(rg), sd->layout.align, sd->layout.size); // ptr

            for (size_t f = 0; f < instr->info.struct_init.fields.len; f++) {
                const MIR_struct_init_field field = instr->info.struct_init.fields.array[f];

                fprintf(qbe_out, "  %%si%luoff%lu =l add %%v%lu, %u\n", instr->info.struct_init.dest, f, instr->info.struct_init.dest, field.offset);

                const SizeAlign sa = calc_size_of(&sd->fields.array[f].type, nct, target_info);
                if (sa.size > 8) {
                    fprintf(qbe_out, "  blit %%v%lu, %%si%luoff%lu, %lu\n", field.temp, instr->info.struct_init.dest, f, sa.size);
                } else {
                    const SV rs = reg_size_to_letter(sa.size);
                    fprintf(qbe_out, "  store"SV_format" %%v%lu, %%si%luoff%lu\n", SV_v_args(rs), field.temp, instr->info.struct_init.dest, f);
                }

            }

            break;
        }
        case MIR_TY__MAKE_ARRAY: {
            /*
            MIR_TY__MAKE_ARRAY:
                stack-allocate (alloc) for TC_TY_Array, or
                call $malloc + stack header { ptr, len, cap } for TC_TY_List (is_heap), then store/blit each
item at i * item_sa.size.
             */
            UNIMPLEMENTED()
        }
        case MIR_TY__MAKE_ENUM: {
            UNIMPLEMENTED()
        }
        case MIR_TY__MAKE_BOX: {
            UNIMPLEMENTED()
        }
        case MIR_TY__CAST: {
            if (instr->info.cast.from_type.ty == TC_TY_Primitive && instr->info.cast.to_type.ty == TC_TY_Primitive) {
                const TC_primitive_classification from = tc_primitive_classification(instr->info.cast.from_type.info.primitive);
                const TC_primitive_classification to = tc_primitive_classification(instr->info.cast.to_type.info.primitive);

                fprintf(qbe_out, "  %%v%lu =", instr->info.cast.dest);

                if (from.is_float_primitive && to.is_float_primitive) {
                    if (from.primitive_byte_size == 8 && to.primitive_byte_size == 4) {
                        fprintf(qbe_out, "=s truncd %%v%lu\n", instr->info.cast.src);
                    } else if (from.primitive_byte_size == 4 && to.primitive_byte_size == 8) {
                        fprintf(qbe_out, "=d exts %%v%lu\n", instr->info.cast.src);
                    } else if (from.primitive_byte_size == to.primitive_byte_size) {
                        fprintf(qbe_out, "=%c copy %%v%lu\n", qbe_val_type(&instr->info.cast.to_type, target_info), instr->info.cast.src);
                    } else {
                        INTERNAL_ERROR()
                    }
                } else if (instr->info.cast.to_type.info.primitive == TC_Primitive__Bool) {
                    const char tc = qbe_val_type(&instr->info.cast.from_type, target_info);

                    fprintf(qbe_out, "=w cne%c %%v%lu\n", tc, instr->info.cast.src);
                } else if (!from.is_float_primitive && to.is_float_primitive) {
                    const char singed = from.is_signed_int_primitive ? 's' : 'u';
                    const char tc = qbe_val_type(&instr->info.cast.from_type, target_info);
                    const char to_c = qbe_val_type(&instr->info.cast.to_type, target_info);

                    fprintf(qbe_out, "=%c %c%ctof %%v%lu\n", to_c, singed, tc, instr->info.cast.src);
                } else if (from.is_float_primitive && !to.is_float_primitive) {
                    const char tc = qbe_val_type(&instr->info.cast.from_type, target_info);
                    const char to_c = qbe_val_type(&instr->info.cast.to_type, target_info);
                    const char singed = to.is_signed_int_primitive ? 's' : 'u';

                    fprintf(qbe_out, "=%c %cto%ci %%v%lu\n", to_c, tc, singed, instr->info.cast.src);
                } else { // into to int

                    // narrowing
                    if (from.primitive_byte_size > to.primitive_byte_size) {
                        if (to.primitive_byte_size < 4) {
                            // emit
                            const char sign = to.is_signed_int_primitive ? 's' : 'u';
                            const char size = to.primitive_byte_size == 1 ? 'b' : 'h';
                            fprintf(qbe_out, "w ext%c%c", sign, size);
                        } else if (to.primitive_byte_size == 4) {
                            fprintf(qbe_out, "w copy");
                        }
                    } else if (from.primitive_byte_size == to.primitive_byte_size) {

                        const char size = to.primitive_byte_size == 8 ? 'l' : 'w';
                        fprintf(qbe_out, "%c copy", size);
                    } else { // widening

                        const char size = to.primitive_byte_size == 8 ? 'l' : 'w';
                        const char sign = to.is_signed_int_primitive ? 's' : 'u';
                        char vsize;
                        switch (to.primitive_byte_size) {
                            case 1: vsize = 'b'; break;
                            case 2: vsize = 'h'; break;
                            case 4: vsize = 'w'; break;
                            case 8: vsize = 'l'; break;
                            default: INTERNAL_ERROR()
                        }
                        fprintf(qbe_out, "%c ext%c%c", size, sign, vsize);
                    }


                    fprintf(qbe_out, " %%v%lu\n", instr->info.cast.src);
                }

                // if (instr->info.cast.from_type.info.primitive == instr->info.cast.to_type.info.primitive) {
                //     fprintf(qbe_out, "  %%v%lu =%c copy %%v%lu\n", instr->info.cast.dest, qbe_val_type(&instr->info.cast.to_type, target_info), instr->info.cast.src);
                // } else if (from.is_float_primitive && to.is_float_primitive) {
                //     if (from.primitive_byte_size == 8 && to.primitive_byte_size == 4) {
                //         fprintf(qbe_out, "  %%v%lu =s truncd %%v%lu\n", instr->info.cast.dest, instr->info.cast.src);
                //     } else if (from.primitive_byte_size == 4 && to.primitive_byte_size == 8) {
                //         fprintf(qbe_out, "  %%v%lu =d exts %%v%lu\n", instr->info.cast.dest, instr->info.cast.src);
                //     } else if (from.primitive_byte_size == to.primitive_byte_size) {
                //         fprintf(qbe_out, "  %%v%lu =%c copy %%v%lu\n", instr->info.cast.dest, qbe_val_type(&instr->info.cast.to_type, target_info), instr->info.cast.src);
                //     } else {
                //         INTERNAL_ERROR()
                //     }
                // } else if (instr->info.cast.to_type.info.primitive == TC_Primitive__Bool) {
                //     const char tc = qbe_val_type(&instr->info.cast.from_type, target_info);
                //
                //     fprintf(qbe_out, "  %%v%lu =w cne%c %%v%lu\n", instr->info.cast.dest, tc, instr->info.cast.src);
                // } else if (!from.is_float_primitive && to.is_float_primitive) {
                //     const char singed = from.is_signed_int_primitive ? 's' : 'u';
                //     const char tc = qbe_val_type(&instr->info.cast.from_type, target_info);
                //     const char to_c = qbe_val_type(&instr->info.cast.to_type, target_info);
                //
                //     fprintf(qbe_out, "  %%v%lu =%c %c%ctof %%v%lu\n", instr->info.cast.dest, to_c, singed, tc, instr->info.cast.src);
                // } else if (from.is_float_primitive && !to.is_float_primitive) {
                //     const char tc = qbe_val_type(&instr->info.cast.from_type, target_info);
                //     const char to_c = qbe_val_type(&instr->info.cast.to_type, target_info);
                //     const char singed = to.is_signed_int_primitive ? 's' : 'u';
                //
                //     fprintf(qbe_out, "  %%v%lu =%c %cto%ci %%v%lu\n", instr->info.cast.dest, to_c, tc, singed, instr->info.cast.src);
                // } else { // Int-to-Int
                //     const char singed = from.is_signed_int_primitive ? 's' : 'u';
                //     const char to_c = qbe_val_type(&instr->info.cast.to_type, target_info);
                //
                //     if (from.primitive_byte_size == 8 && to.primitive_byte_size == 4) {
                //         fprintf(qbe_out, "  %%v%lu =w copy %%v%lu\n", instr->info.cast.dest, instr->info.cast.src);
                //     } else {
                //         const SV rl = reg_size_to_letter(from.primitive_byte_size);
                //         fprintf(qbe_out, "  %%v%lu =%c ext%c"SV_format" %%v%lu\n",
                //             instr->info.cast.dest,
                //             to_c,
                //             singed,
                //             SV_v_args(rl),
                //             instr->info.cast.src
                //         );
                //     }
                //
                // }
            } else {
                INTERNAL_ERROR()
            }
            break;
        }
        case MIR_TY__OP_BINOP: {
            if (instr->info.binop.type.ty != TC_TY_Primitive) {
                INTERNAL_ERROR()
            }
            const SizeAlign sa = calc_size_of(&instr->info.binop.type, nct, target_info);
            const SV val_st_sz = reg_size_to_letter(sa.align);
            SV op;
            switch (instr->info.binop.op) {
                case MIR_BINOP_ADD: { op = (SV) {.start="add", .len=3}; break; }
                case MIR_BINOP_SUB: { op = (SV) {.start="sub", .len=3}; break; }
                case MIR_BINOP_MUL: { op = (SV) {.start="mul", .len=3}; break; }

                case MIR_BINOP_DIV: {
                    switch (instr->info.binop.type.info.primitive) {
                        case TC_Primitive__Int8:
                        case TC_Primitive__Int16:
                        case TC_Primitive__Int32:
                        case TC_Primitive__Int64:
                        case TC_Primitive__Float32:
                        case TC_Primitive__Float64: {
                            op = (SV) {.start="div", .len=3};
                            break;
                        }
                        case TC_Primitive__Uint8:
                        case TC_Primitive__Uint16:
                        case TC_Primitive__Uint32:
                        case TC_Primitive__Uint64:
                        case TC_Primitive__Char:
                        case TC_Primitive__Bool: {
                            op = (SV) {.start="udiv", .len=4};
                            break;
                        }
                    }

                    break;
                }
                case MIR_BINOP_MOD: {
                    switch (instr->info.binop.type.info.primitive) {
                        case TC_Primitive__Int8:
                        case TC_Primitive__Int16:
                        case TC_Primitive__Int32:
                        case TC_Primitive__Int64:
                        case TC_Primitive__Float32:
                        case TC_Primitive__Float64: {
                            op = (SV) {.start="rem", .len=3};
                            break;
                        }
                        case TC_Primitive__Uint8:
                        case TC_Primitive__Uint16:
                        case TC_Primitive__Uint32:
                        case TC_Primitive__Uint64:
                        case TC_Primitive__Char:
                        case TC_Primitive__Bool: {
                            op = (SV) {.start="urem", .len=4};
                            break;
                        }
                    }
                    break;
                }
                // case MIR_BINOP_POW: { op = (SV) {.start="pow", .len=3}; break; }

                case MIR_BINOP_POW:
                case MIR_BINOP_BIT_AND:
                case MIR_BINOP_BIT_OR:
                case MIR_BINOP_BIT_XOR:
                case MIR_BINOP_SHL:
                case MIR_BINOP_SHR: { UNIMPLEMENTED() }
            }
            fprintf(qbe_out, "  %%v%lu ="SV_format" "SV_format" %%v%lu, %%v%lu\n", instr->info.binop.dest, SV_v_args(val_st_sz), SV_v_args(op), instr->info.binop.a, instr->info.binop.b);
            break;
        }
        case MIR_TY__OP_CMP: {
            if (instr->info.cmp.type.ty != TC_TY_Primitive) INTERNAL_ERROR()

            fprintf(qbe_out, "  %%v%lu =w c", instr->info.cmp.dest);

            if (instr->info.cmp.cmp_kind != MIR_CMP_EQ && instr->info.cmp.cmp_kind != MIR_CMP_NOT_EQ) {
                switch (instr->info.cmp.type.info.primitive) {
                    case TC_Primitive__Int8:
                    case TC_Primitive__Int16:
                    case TC_Primitive__Int32:
                    case TC_Primitive__Int64: {
                        fprintf(qbe_out, "s");
                        break;
                    }
                    case TC_Primitive__Uint8:
                    case TC_Primitive__Uint16:
                    case TC_Primitive__Uint32:
                    case TC_Primitive__Uint64:
                    case TC_Primitive__Char:
                    case TC_Primitive__Bool: {
                        fprintf(qbe_out, "u");
                        break;
                    }

                    case TC_Primitive__Float32:
                    case TC_Primitive__Float64: {
                        // nothing
                        break;
                    }
                }
            }

            switch (instr->info.cmp.cmp_kind) {
                case MIR_CMP_EQ: {
                    fprintf(qbe_out, "eq");
                    break;
                }
                case MIR_CMP_NOT_EQ: {
                    fprintf(qbe_out, "ne");
                    break;
                }
                case MIR_CMP_LESS: {
                    fprintf(qbe_out, "lt");
                    break;
                }
                case MIR_CMP_LESS_EQ: {
                    fprintf(qbe_out, "le");
                    break;
                }
                case MIR_CMP_GREATER: {
                    fprintf(qbe_out, "gt");
                    break;
                }
                case MIR_CMP_GREATER_EQ: {
                    fprintf(qbe_out, "ge");
                    break;
                }
            }

            const SizeAlign sa = calc_size_of(&instr->info.cmp.type, nct, target_info);
            const SV rg = reg_size_to_letter(sa.size);
            fprintf(qbe_out, SV_format" ", SV_v_args(rg));

            fprintf(qbe_out, "%%v%lu, %%v%lu\n", instr->info.cmp.a, instr->info.cmp.b);

            break;
        }
        case MIR_TY__BRANCH: {
            fprintf(qbe_out, "  jnz %%v%lu, @block_%lu, @block_%lu\n", instr->info.branch.cond, instr->info.branch.branch_true, instr->info.branch.branch_false);
            break;
        }
        case MIR_TY__JUMP: {
            fprintf(qbe_out, "  jmp @block_%lu\n", instr->info.jump.target_block);
            break;
        }
        case MIR_TY__CALL: {
            if (instr->info.call.has_dest) {
                const SizeAlign sa = calc_size_of(&instr->info.call.ret_type, nct, target_info);
                const SV val_st_sz = reg_size_to_letter(sa.align);
                fprintf(qbe_out, "  %%v%lu ="SV_format" ", instr->info.call.dest, SV_v_args(val_st_sz));
            } else {
                fprintf(qbe_out, "  ");
            }
            fprintf(qbe_out, "call $"SV_format" (", SV_v_args(instr->info.call.raw_name));
            for (size_t n = 0; n < instr->info.call.args.len; n++) {
                // const char ck = qbe_val_type(&instr->info.call.arg_types.array[n], target_info);
                // char ck[2];
                // qbe_fn_call_type(&instr->info.call.arg_types.array[n], target_info, ck);

                write_type(&instr->info.call.arg_types.array[n], qbe_mat_needing_types, qbe_out, target_info);

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
        }
        case MIR_TY__RETURN: {
            fprintf(qbe_out, "  ret %%v%lu\n", instr->info.return_val.val);
            break;
        }
        case MIR_TY__RETURN_VOID: {
            fprintf(qbe_out, "  ret\n");
            break;
        }
        case MIR_TY__PHI: {
            SV rg;
            if (instr->info.phi.type.ty == TC_TY_Ref) {
                rg = reg_size_to_letter(target_info->ptr_size);
            } else if (instr->info.phi.type.ty == TC_TY_Primitive) {
                const SizeAlign sa = calc_size_of(&instr->info.phi.type, nct, target_info);
                rg = reg_size_to_letter(sa.size);
            } else {
                UNIMPLEMENTED()
            }
            fprintf(qbe_out, "  %%v%lu ="SV_format" phi ", instr->info.phi.dest, SV_v_args(rg));
            for (size_t n = 0; n < instr->info.phi.args.len; n++) {
                fprintf(qbe_out, "@block_%lu %%v%lu", instr->info.phi.args.array[n].block_id, instr->info.phi.args.array[n].temp);
                if (n + 1 != instr->info.phi.args.len) {
                    fprintf(qbe_out, ", ");
                }
            }
            fprintf(qbe_out, "\n");
            break;
        }
        case MIR_TY__DROP:
            break;
    }
}

void mir_emit_type_def(const TC_typeid* t, const TC_TypeTable* nct, const TargetInfo* target_info, const khash_t(sv_u64_map)* identifier_map, FILE* restrict qbe_type_out);

// void mir_emit_type_table(const TC_typeid_LIST* restrict qbe_mat_needing_types, const TC_TypeTable* restrict nct, FILE* restrict qbe_type_out, const TargetInfo* restrict target_info, const khash_t(sv_u64_map)* restrict identifier_map) {
//
//     for (size_t n = 0; n < qbe_mat_needing_types->len; n++) {
//         const SizeAlign sa = calc_size_of(&qbe_mat_needing_types->array[n], nct, target_info);
//         fprintf(qbe_type_out, "type :t%lu = align %lu {", n, sa.align);
//
//         mir_emit_type_def(&qbe_mat_needing_types->array[n], nct, target_info, identifier_map, qbe_type_out);
//
//         const SV t_sv = format_TC_typeid(&qbe_mat_needing_types->array[n], identifier_map, nct);
//         fprintf(qbe_type_out, "} # '"SV_format"'\n", SV_v_args(t_sv));
//     }
//
// }

void mir_emit_type_def(const TC_typeid* t, const TC_TypeTable* nct, const TargetInfo* target_info, const khash_t(sv_u64_map)* identifier_map, FILE* restrict qbe_type_out) {
    switch (t->ty) {
        case TC_TY_Void: INTERNAL_ERROR()
        case TC_TY_Primitive: {
            write_type(t, NULL, qbe_type_out, target_info);
            break;
        }
        case TC_TY_Struct:
            break;
        case TC_TY_Enum:
            break;
        case TC_TY_Ref: {
            const SV rg = reg_size_to_letter(target_info->ptr_size);
            fprintf(qbe_type_out, SV_format, SV_v_args(rg));
            break;
        }
        case TC_TY_Array:
            break;
        case TC_TY_List:
            break;
        case TC_TY_Box:
            break;
        case TC_TY_Slice:
            break;
        case TC_TY_External_Type:
            break;
    }
}