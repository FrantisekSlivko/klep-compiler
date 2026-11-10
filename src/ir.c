#include "ir.h"
#include "typecompletion.h"
#include "passes/parser/parser.h"

#include "utils/color.h"
#include "utils/dev.h"
#include <ctype.h>

#include "utils/str.h"

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


#define MAKE_SV_LIT(X) (SV){.start=X, .len=sizeof(X)-1}
SV format_IR_typeuse_inner(const SV buffer, const IR_typeuse* restrict ty, const khash_t(sv_u64_map)* restrict identifier_map) {
    switch (ty->tt_ty) {
        case IR_TY_Void: {
            return append_and_advance_buff(&buffer, MAKE_SV_LIT("Void"));
        }
        case IR_TY_Primitive: {
            return append_and_advance_buff(&buffer, TC_Primitive_TY_to_SV(ty->info.primitive));
        }
        case IR_TY_Named: {
            return append_and_advance_buff(&buffer, ty->info.named.span.sv);
        }
        case IR_TY_Ref:
        case IR_TY_Box:
        case IR_TY_Slice:
        case IR_TY_List: {
            const SV v1 = append_and_advance_buff(&buffer, ty->info.named.span.sv);
            const SV v2 = append_and_advance_buff(&v1, MAKE_SV_LIT("<"));
            const SV v3 = format_IR_typeuse_inner(v2, ty->contained, identifier_map);
            return append_and_advance_buff(&v3, MAKE_SV_LIT(">"));
        }
        case IR_TY_Array: {
            const SV v1 = append_and_advance_buff(&buffer, ty->info.named.span.sv);
            const SV v2 = append_and_advance_buff(&v1, MAKE_SV_LIT("<"));
            const SV v3 = format_IR_typeuse_inner(v2, ty->contained, identifier_map);
            const SV v4 = append_and_advance_buff(&v3, MAKE_SV_LIT(", "));
            const int len = snprintf((char*)v4.start, v4.len, "{%u}", ty->array_const_size);
            const SV v5 = (SV){.start = v4.start + len, .len = v4.len - len };
            return append_and_advance_buff(&v5, MAKE_SV_LIT(">"));
        }
        case IR_TY_Generic: {
            const SV v1 = append_and_advance_buff(&buffer, ty->info.named.span.sv);
            const SV v2 = append_and_advance_buff(&v1, MAKE_SV_LIT("<"));
            SV nx = v2;
            for (size_t i=0; i<ty->contained_len; i++) {
                nx = format_IR_typeuse_inner(nx, &ty->contained[i], identifier_map);
                if (i+1 != ty->contained_len) {
                    nx = append_and_advance_buff(&nx, MAKE_SV_LIT(", "));
                }
            }
            return append_and_advance_buff(&nx, MAKE_SV_LIT(">"));
        }
    }
    UNREACHABLE()
}

SV format_IR_typeuse(const IR_typeuse* restrict ty, const khash_t(sv_u64_map)* restrict identifier_map) {
    const size_t MAX_SIZE = 1024;
    char* buff = malloc(sizeof(char)*MAX_SIZE);
    const SV buffer = (SV) {.start = buff, .len = MAX_SIZE };
    const SV ret = format_IR_typeuse_inner(buffer, ty, identifier_map);
    char* new_buff = realloc(buff, ret.start - buffer.start);
    return (SV) { .start = new_buff, .len = ret.start - buffer.start};
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
            const SV d_t_sv = format_IR_typeuse(instr->info.drop.type, identifier_map);
            printf("DROP $"SV_format" %%%lu '"SV_format"'", SV_v_args(instr->info.drop.var_name.span.sv), instr->info.drop.dest.val, SV_v_args(d_t_sv));
            break;
        }

        case IR_INST_TY__MOVE: {
            printf("%%%lu = MOVE %%%lu", instr->info.move.dest.val, instr->info.move.src.val);
            break;
        }

        case IR_INST_TY__ASSIGN: {
            printf("%%%lu = ASSIGN .old=", instr->info.assign.dest.val);
            if (instr->info.assign.old.val == 0) {
                printf("None");
            } else {
                printf("%%%lu", instr->info.assign.old.val);
            }
            printf(", .new=%%%lu", instr->info.assign.new.val);
            break;
        }

        case IR_INST_TY__REF_OF: {
            printf("%%%lu = REF_OF %%%lu", instr->info.ref_of.dest.val, instr->info.ref_of.src.val);
            break;
        }

        case IR_INST_TY__VAR_DECL: {
            const SV t_sv = format_IR_typeuse(instr->info.var_decl.type, identifier_map);
            printf("VAR_DECL $"SV_format" %%%lu {"SV_format"}", SV_v_args(instr->info.var_decl.var_name.span.sv), instr->info.var_decl.dest.val, SV_v_args(t_sv));
            break;
        }

        case IR_INST_TY__EXPLICIT_CAST: {
            const SV t_sv = format_IR_typeuse(instr->info.explicit_cast.type, identifier_map);
            printf("%%%lu = CAST{"SV_format"}(%%%lu)", instr->info.explicit_cast.dest.val, SV_v_args(t_sv), instr->info.explicit_cast.src.val);
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
            const SV st_sv = format_IR_typeuse(instr->info.make_struct.type, identifier_map);
            printf("%%%lu = MAKE_STRUCT '"SV_format"' {", instr->info.make_struct.dest.val, SV_v_args(st_sv));
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
            const SV variant = SV_from_identifiermap_lookup(identifier_map,instr->info.make_enum.variant);
            const SV e_t_sv = format_IR_typeuse(instr->info.make_enum.type, identifier_map);
            printf("%%%lu = MAKE_ENUM '"SV_format"'::'"SV_format"' ", instr->info.make_enum.dest.val, SV_v_args(e_t_sv), SV_v_args(variant));
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
        case IR_INST_TY__MATCH_META__TARGET: {
            printf("META: MATCH: TARGET %%%lu", instr->info.match_meta__target.target.val);
            break;
        }
        case IR_INST_TY__MATCH_META__MATCH: {
            printf("META: MATCH (%%%lu) { ...patterns }", instr->info.match_meta__target.target.val);
            break;
        }
        case ENUM_GET_FIELD_PTR: {
            printf("%%%lu = ENUM_GET_FIELD_PTR from %%%lu as "SV_format"::"SV_format" %u-nth value",
                instr->info.enum_get_field_ptr.dest.val,
                instr->info.enum_get_field_ptr.enum_src.val,
                SV_v_args(instr->info.enum_get_field_ptr.enum_name.span.sv), SV_v_args(instr->info.enum_get_field_ptr.variant_name.span.sv), instr->info.enum_get_field_ptr.value_nth);
            break;
        }
        case IR_INST_TY__ENUM_VARIANT_NUMBER_EXTRACT: {
            printf("%%%lu = ENUM_VARIANT_NUMBER of enum %%%lu", instr->info.enum_variant_number_extract.dest.val, instr->info.enum_variant_number_extract.enum_src.val);
            break;
        }
        case IR_INST_TY__ENUM_VARIANT_NUMBER_FROM_TYPE: {
            const SV enum_name = SV_from_identifiermap_lookup(identifier_map, instr->info.enum_variant_number_from_type.enum_name.identifier);
            const SV variant_name = SV_from_identifiermap_lookup(identifier_map, instr->info.enum_variant_number_from_type.variant_name.identifier);
            printf("%%%lu = ENUM_VARIANT_NUMBER of '"SV_format"'::'"SV_format"'", instr->info.enum_variant_number_from_type.dest.val, SV_v_args(enum_name), SV_v_args(variant_name));
            break;
        }
        case IR_INST_TY__MAKE_BOX: {
            printf("%%%lu = MAKE_BOX %%%lu", instr->info.make_box.dest.val, instr->info.make_box.src.val);
            break;
        }
    }
}

