#include "ir.h"

#include "utils/color.h"
#include "utils/dev.h"
#include <ctype.h>

void print_IR_typeuse(const IR_typeuse* restrict ty, const khash_t(sv_u64_map)* restrict identifier_map) {
    if (ty->tt_ty == IR_TY_Primitive) {
        const SV k = TC_Primitive_TY_to_SV(ty->info.primitive);
        printf(SV_format, SV_v_args(k));
        return;
    }
    if (ty->tt_ty == IR_TY_Void) {
        printf("Void");
        return;
    }
    const SV type_name = ty->info.named.span.sv; // SV_from_identifiermap_lookup(identifier_map, ty->info.named.name);
    printf(SV_format, SV_v_args(type_name));
    switch (ty->tt_ty) {
        case IR_TY_Primitive:
        case IR_TY_Named:
            break;
        case IR_TY_Ref:
        case IR_TY_Box:
        case IR_TY_Slice:
        case IR_TY_List: {
            putc('<', stdout);
            print_IR_typeuse(ty->contained, identifier_map);
            putc('>', stdout);
            break;
        }
        case IR_TY_Array: {
            putc('<', stdout);
            print_IR_typeuse(ty->contained, identifier_map);
            printf(", %u>", ty->array_const_size);
            break;
        }
        case IR_TY_Generic: {
            putc('<', stdout);
            for (size_t i=0; i<ty->contained_len; i++) {
                print_IR_typeuse(&ty->contained[i], identifier_map);
                if (i+1 != ty->contained_len) printf(", ");
            }
            putc('>', stdout);
            break;
        }
        case IR_TY_Void: {
            UNREACHABLE();
        }
    }
}

void print_IR_instr(const IR_instr* restrict instr, const khash_t(sv_u64_map)* restrict identifier_map) {
    switch (instr->ty) {
        case IR_INST_TY__INVALID: {
            printf(C_RED"INVALID IR"C_RESET);
            break;
        }

        case IR_INST_TY__PHI: {
            printf("%%%lu = PHI { ", instr->info.phi.dest.val);
            for (size_t n = 0; n < instr->info.phi.args->len; n++) {
                const IR_temporal_block_phi_record val = instr->info.phi.args->array[n];
                printf("%%%lu BLOCK<%lu>", val.temp.val, val.block);
                if (n+1 < instr->info.phi.args->len) {
                    printf(", ");
                }
            }
            printf("}");
            break;
        }

        case IR_INST_TY__DROP: {
            printf("DROP $"SV_format" %%%lu '"SV_format"'", SV_v_args(instr->info.drop.var_name.span.sv), instr->info.drop.dest.val, SV_v_args(instr->info.drop.type->full_typeuse_span.sv));
            break;
        }

        case IR_INST_TY__MOVE: {
            printf("%%%lu = MOVE %%%lu", instr->info.move.dest.val, instr->info.move.src.val);
            break;
        }

        case IR_INST_TY__VAR_DECL: {
            printf("VAR_DECL $"SV_format" %%%lu {"SV_format"}", SV_v_args(instr->info.var_decl.var_name.span.sv), instr->info.var_decl.dest.val, SV_v_args(instr->info.var_decl.type->full_typeuse_span.sv));
            break;
        }

        case IR_INST_TY__EXPLICIT_CAST: {
            printf("%%%lu = CAST{"SV_format"}(%%%lu)", instr->info.explicit_cast.dest.val, SV_v_args(instr->info.explicit_cast.type->full_typeuse_span.sv), instr->info.explicit_cast.src.val);
            break;
        }

        case IR_INST_TY__STORE_PTR: {
            printf("STORE_PTR INTO %%%lu VALUE %%%lu", instr->info.store_ptr.ptr.val, instr->info.store_ptr.value.val);
            break;
        }




        case IR_INST_TY__CONST_INT: {
            printf("%%%lu = CONST_INT %li", instr->info.const_int.dest.val, instr->info.const_int.val);
            break;
        }
        case IR_INST_TY__CONST_FLOAT: {
            printf("%%%lu = CONST_FLOAT %f", instr->info.const_float.dest.val, instr->info.const_float.val);
            break;
        }
        case IR_INST_TY__CONST_STRING: {
            printf("%%%lu = CONST_STRING \"", instr->info.const_string.dest.val);
            const SV val = SV_from_identifiermap_lookup(identifier_map, instr->info.const_string.string);
            for (size_t i = 0; i < val.len; i++) {
                unsigned char c = val.start[i];
                if (c == '\n') printf("\\n");
                else if (c == '\t') printf("\\t");
                else if (c == '\r') printf("\\r");
                else if (c == '\\') printf("\\\\");
                else if (isprint(c)) putchar(c);
                else printf("\\x%02X", c);
            }
            printf("\"");
            break;
        }
        case IR_INST_TY__MAKE_ARRAY: {
            printf("%%%lu = MAKE_ARRAY [", instr->info.make_array.dest.val);
            for (size_t i = 0; i < instr->info.make_array.items->len; i++) {
                printf("%%%lu", instr->info.make_array.items->array[i].val);
                if (i+1 != instr->info.make_array.items->len) {
                    printf(", ");
                }
            }
            putc(']' ,stdout);
            break;
        }
        case IR_INST_TY__MAKE_STRUCT: {
            printf("%%%lu = MAKE_STRUCT '"SV_format"' {", instr->info.make_struct.dest.val, SV_v_args(instr->info.make_struct.type->full_typeuse_span.sv));
            for (size_t i = 0; i < instr->info.make_struct.args->len; i++) {
                const IR_named_value v = instr->info.make_struct.args->array[i];
                printf("'."SV_format"' = %%%lu", SV_v_args(v.name.span.sv), v.value.val);
                if (i+1 != instr->info.make_struct.args->len) {
                    printf(", ");
                }
            }
            putc('}' ,stdout);
            break;
        }
        case IR_INST_TY__MAKE_ENUM: {
            printf("%%%lu = MAKE_ENUM '"SV_format"' ", instr->info.make_enum.dest.val, SV_v_args(instr->info.make_enum.type->full_typeuse_span.sv));
            if (instr->info.make_enum.args->len == 0) {
                printf("()");
                break;
            }
            printf("( ");
            for (size_t n = 0; n < instr->info.make_enum.args->len; n++) {
                printf("%%%lu", instr->info.make_enum.args->array[n].val);
                if (n + 1 != instr->info.make_enum.args->len) {
                    printf(", ");
                }
            }
            printf(" )");
            break;
        }


        case IR_INST_TY__OP_ADD: {
            printf("%%%lu = ADD %%%lu, %%%lu", instr->info.math_cmp_boolean_op.dest.val, instr->info.math_cmp_boolean_op.src1.val, instr->info.math_cmp_boolean_op.src2.val);
            break;
        }
        case IR_INST_TY__OP_SUB: {
            printf("%%%lu = SUB %%%lu, %%%lu", instr->info.math_cmp_boolean_op.dest.val, instr->info.math_cmp_boolean_op.src1.val, instr->info.math_cmp_boolean_op.src2.val);
            break;
        }
        case IR_INST_TY__OP_DIV: {
            printf("%%%lu = DIV %%%lu, %%%lu", instr->info.math_cmp_boolean_op.dest.val, instr->info.math_cmp_boolean_op.src1.val, instr->info.math_cmp_boolean_op.src2.val);
            break;
        }
        case IR_INST_TY__OP_MUL: {
            printf("%%%lu = MUL %%%lu, %%%lu", instr->info.math_cmp_boolean_op.dest.val, instr->info.math_cmp_boolean_op.src1.val, instr->info.math_cmp_boolean_op.src2.val);
            break;
        }
        case IR_INST_TY__OP_POW: {
            printf("%%%lu = POW %%%lu, %%%lu", instr->info.math_cmp_boolean_op.dest.val, instr->info.math_cmp_boolean_op.src1.val, instr->info.math_cmp_boolean_op.src2.val);
            break;
        }


        case IR_INST_TY__OP_OR: {
            printf("%%%lu = OR %%%lu, %%%lu", instr->info.math_cmp_boolean_op.dest.val, instr->info.math_cmp_boolean_op.src1.val, instr->info.math_cmp_boolean_op.src2.val);
            break;
        }
        case IR_INST_TY__OP_AND: {
            printf("%%%lu = AND %%%lu, %%%lu", instr->info.math_cmp_boolean_op.dest.val, instr->info.math_cmp_boolean_op.src1.val, instr->info.math_cmp_boolean_op.src2.val);
            break;
        }



        case IR_INST_TY__OP_CMP_EQ: {
            printf("%%%lu = CMP_EQ %%%lu, %%%lu", instr->info.math_cmp_boolean_op.dest.val, instr->info.math_cmp_boolean_op.src1.val, instr->info.math_cmp_boolean_op.src2.val);
            break;
        }
        case IR_INST_TY__OP_CMP_NOT_EQ: {
            printf("%%%lu = CMP_NOT_EQ %%%lu, %%%lu", instr->info.math_cmp_boolean_op.dest.val, instr->info.math_cmp_boolean_op.src1.val, instr->info.math_cmp_boolean_op.src2.val);
            break;
        }
        case IR_INST_TY__OP_CMP_LESS: {
            printf("%%%lu = CMP_LESS %%%lu, %%%lu", instr->info.math_cmp_boolean_op.dest.val, instr->info.math_cmp_boolean_op.src1.val, instr->info.math_cmp_boolean_op.src2.val);
            break;
        }
        case IR_INST_TY__OP_CMP_LESS_EQ: {
            printf("%%%lu = CMP_LESS_EQ %%%lu, %%%lu", instr->info.math_cmp_boolean_op.dest.val, instr->info.math_cmp_boolean_op.src1.val, instr->info.math_cmp_boolean_op.src2.val);
            break;
        }
        case IR_INST_TY__OP_CMP_GREATER: {
            printf("%%%lu = CMP_GREATER %%%lu, %%%lu", instr->info.math_cmp_boolean_op.dest.val, instr->info.math_cmp_boolean_op.src1.val, instr->info.math_cmp_boolean_op.src2.val);
            break;
        }
        case IR_INST_TY__OP_CMP_GREATER_EQ: {
            printf("%%%lu = CMP_GREATER_EQ %%%lu, %%%lu", instr->info.math_cmp_boolean_op.dest.val, instr->info.math_cmp_boolean_op.src1.val, instr->info.math_cmp_boolean_op.src2.val);
            break;
        }



        case IR_INST_TY__BRANCH: {
            printf("BR %%%lu, BLOCK<%lu>, BLOCK<%lu>", instr->info.branch.cond.val, instr->info.branch.branch_true, instr->info.branch.branch_false);
            break;
        }
        case IR_INST_TY__JUMP: {
            printf("JUMP BLOCK<%lu>", instr->info.jump.block);
            break;
        }

        case IR_INST_TY__RETURN_VOID: {
            printf("RETURN VOID");
            break;
        }
        case IR_INST_TY__RETURN: {
            printf("RETURN %%%lu", instr->info.return_value.val);
            break;
        }
        case IR_INST_TY__GET_STRUCT_FIELD_PTR: {
            printf("%%%lu = GET_FIELD_PTR %%%lu, '."SV_format"'", instr->info.get_struct_field_ptr.dest.val, instr->info.get_struct_field_ptr.struct_ptr.val, SV_v_args(instr->info.get_struct_field_ptr.field_name.span.sv));
            break;
        }
        case IR_INST_TY__INDEX_PTR: {
            printf("%%%lu = INDEX_PTR %%%lu [ %%%lu ]", instr->info.index_ptr.dest.val, instr->info.index_ptr.array_ptr.val, instr->info.index_ptr.index_val.val);
            break;
        }
        case IR_INST_TY__READ_PTR: {
            printf("%%%lu = READ_PTR %%%lu", instr->info.read_ptr.dest.val, instr->info.read_ptr.ptr.val);
            break;
        }
        case IR_INST_TY__FN_CALL_RETVOID: {
            printf("CALL <"SV_format"> (", SV_v_args(instr->info.fn_call_retvoid.name.span.sv));
            for (size_t i = 0; i < instr->info.fn_call_retvoid.args->len; i++) {
                printf("%%%lu", instr->info.fn_call_retvoid.args->array[i].val);
                if (i+1 != instr->info.fn_call_retvoid.args->len) {
                    printf(", ");
                }
            }
            putc(')' ,stdout);
            break;
        }
        case IR_INST_TY__FN_CALL_RETVAL: {
            printf("%%%lu = CALL <"SV_format"> (", instr->info.fn_call_retval.dest.val, SV_v_args(instr->info.fn_call_retval.name.span.sv));
            for (size_t i = 0; i < instr->info.fn_call_retval.args->len; i++) {
                printf("%%%lu", instr->info.fn_call_retval.args->array[i].val);
                if (i+1 != instr->info.fn_call_retval.args->len) {
                    printf(", ");
                }
            }
            putc(')' ,stdout);
            break;
        }
    }
}

