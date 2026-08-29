#include "ir.h"

#include "utils/color.h"
#include "utils/dev.h"


void print_IR_typeuse(const IR_typeuse* restrict ty, khash_t(sv_u64_map)* restrict identifier_map) {
    if (ty->tt_ty == IR_TY_Primitive) {
        const SV k = TC_Primitive_TY_to_SV(ty->info.primitive);
        printf(SV_format, SV_v_args(k));
        return;
    }
    if (ty->tt_ty == IR_TY_Void) {
        printf("Void");
        return;
    }
    const SV type_name = SV_from_identifiermap_lookup(identifier_map, ty->info.named.name);
    printf(SV_format, SV_v_args(type_name));
    switch (ty->tt_ty) {
        case IR_TY_Primitive:
        case IR_TY_Named:
            break;
        case IR_TY_Ref:
        case IR_TY_Array:
        case IR_TY_Box:
        case IR_TY_Slice:
        case IR_TY_List: {
            putc('<', stdout);
            print_IR_typeuse(ty->contained, identifier_map);
            putc('>', stdout);
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

void print_IR_instr(const IR_instr* restrict instr, khash_t(sv_u64_map)* restrict identifier_map) {
    switch (instr->ty) {
        case IR_INST_TY__INVALID: {
            printf(C_RED"INVALID IR"C_RESET);
            break;
        }


        case IR_INST_TY__PHI: {
            printf("%%%lu = PHI { ", instr->dest.val);
            for (size_t n = 0; n < instr->p2.phi_args->len; n++) {
                const IR_temporal_block_phi_record val = instr->p2.phi_args->array[n];
                printf("%%%lu BLOCK<%lu>", val.temp.val, val.block);
                if (n+1 < instr->p2.phi_args->len) {
                    printf(", ");
                }
            }
            printf("}");
            break;
        }

        case IR_INST_TY__VAR_DECL: {
            const SV var_name = SV_from_identifiermap_lookup(identifier_map, instr->p1.name);
            printf("VAR_DECL $"SV_format" %%%lu {", SV_v_args(var_name), instr->dest.val);
            print_IR_typeuse(instr->p2.type, identifier_map);
            printf("}");
            break;
        }



        case IR_INST_TY__STORE_PTR: {
            printf("STORE_PTR INTO %%%lu VALUE %%%lu", instr->dest.val, instr->p1.src1.val);
            break;
        }





        case IR_INST_TY__CONST_INT: {
            printf("%%%lu = CONST_INT %li", instr->dest.val, instr->p1.value);
            break;
        }
        case IR_INST_TY__CONST_FLOAT: {
            printf("%%%lu = CONST_FLOAT %f", instr->dest.val, instr->p1.value_float);
            break;
        }
        case IR_INST_TY__MAKE_ARRAY: {
            printf("%%%lu = MAKE_ARRAY [", instr->dest.val);
            for (size_t i = 0; i < instr->p2.array->len; i++) {
                printf("%%%lu", instr->p2.array->array[i].val);
                if (i+1 != instr->p2.array->len) {
                    printf(", ");
                }
            }
            putc(']' ,stdout);
            break;
        }
        case IR_INST_TY__MAKE_STRUCT: {
            const SV name = SV_from_identifiermap_lookup(identifier_map, instr->p1.name);
            printf("%%%lu = MAKE_STRUCT '"SV_format"' {", instr->dest.val, SV_v_args(name));
            for (size_t i = 0; i < instr->p2.struct_args->len; i++) {
                const IR_named_value v = instr->p2.struct_args->array[i];
                const SV field_name = SV_from_identifiermap_lookup(identifier_map, v.name);
                printf("'."SV_format"' = %%%lu", SV_v_args(field_name), v.value.val);
                // printf("%%%lu", instr->p2.array->array[i].val);
                if (i+1 != instr->p2.struct_args->len) {
                    printf(", ");
                }
            }
            putc('}' ,stdout);
            break;
        }


        case IR_INST_TY__OP_ADD: {
            printf("%%%lu = ADD %%%lu, %%%lu", instr->dest.val, instr->p1.src1.val, instr->p2.src2.val);
            break;
        }
        case IR_INST_TY__OP_SUB: {
            printf("%%%lu = SUB %%%lu, %%%lu", instr->dest.val, instr->p1.src1.val, instr->p2.src2.val);
            break;
        }
        case IR_INST_TY__OP_DIV: {
            printf("%%%lu = DIV %%%lu, %%%lu", instr->dest.val, instr->p1.src1.val, instr->p2.src2.val);
            break;
        }
        case IR_INST_TY__OP_MUL: {
            printf("%%%lu = MUL %%%lu, %%%lu", instr->dest.val, instr->p1.src1.val, instr->p2.src2.val);
            break;
        }
        case IR_INST_TY__OP_POW: {
            printf("%%%lu = POW %%%lu, %%%lu", instr->dest.val, instr->p1.src1.val, instr->p2.src2.val);
            break;
        }


        case IR_INST_TY__OP_OR: {
            printf("%%%lu = OR %%%lu, %%%lu", instr->dest.val, instr->p1.src1.val, instr->p2.src2.val);
            break;
        }
        case IR_INST_TY__OP_AND: {
            printf("%%%lu = AND %%%lu, %%%lu", instr->dest.val, instr->p1.src1.val, instr->p2.src2.val);
            break;
        }



        case IR_INST_TY__OP_CMP_EQ: {
            printf("%%%lu = CMP_EQ %%%lu, %%%lu", instr->dest.val, instr->p1.src1.val, instr->p2.src2.val);
            break;
        }
        case IR_INST_TY__OP_CMP_NOT_EQ: {
            printf("%%%lu = CMP_NOT_EQ %%%lu, %%%lu", instr->dest.val, instr->p1.src1.val, instr->p2.src2.val);
            break;
        }
        case IR_INST_TY__OP_CMP_LESS: {
            printf("%%%lu = CMP_LESS %%%lu, %%%lu", instr->dest.val, instr->p1.src1.val, instr->p2.src2.val);
            break;
        }
        case IR_INST_TY__OP_CMP_LESS_EQ: {
            printf("%%%lu = CMP_LESS_EQ %%%lu, %%%lu", instr->dest.val, instr->p1.src1.val, instr->p2.src2.val);
            break;
        }
        case IR_INST_TY__OP_CMP_GREATER: {
            printf("%%%lu = CMP_GREATER %%%lu, %%%lu", instr->dest.val, instr->p1.src1.val, instr->p2.src2.val);
            break;
        }
        case IR_INST_TY__OP_CMP_GREATER_EQ: {
            printf("%%%lu = CMP_GREATER_EQ %%%lu, %%%lu", instr->dest.val, instr->p1.src1.val, instr->p2.src2.val);
            break;
        }



        case IR_INST_TY__BRANCH: {
            printf("BR %%%lu, BLOCK<%lu>, BLOCK<%lu>", instr->dest.val, instr->p1.br_true, instr->p2.br_false);
            break;
        }
        case IR_INST_TY__JUMP: {
            printf("JUMP BLOCK<%lu>", instr->p1.br_true);
            break;
        }

        case IR_INST_TY__RETURN_VOID: {
            printf("RETURN VOID");
            break;
        }
        case IR_INST_TY__RETURN: {
            printf("RETURN %%%lu", instr->p1.src1.val);
            break;
        }
        case IR_INST_TY__GET_FIELD_PTR: {
            const SV field_name = SV_from_identifiermap_lookup(identifier_map, instr->p1.name);
            printf("%%%lu = GET_FIELD_PTR %%%lu, '."SV_format"'", instr->dest.val, instr->p2.src2.val, SV_v_args(field_name));
            break;
        }
        case IR_INST_TY__INDEX_PTR: {
            printf("%%%lu = INDEX_PTR %%%lu [ %%%lu ]", instr->dest.val, instr->p1.src1.val, instr->p2.src2.val);
            break;
        }
        case IR_INST_TY__READ_PTR: {
            printf("%%%lu = READ_PTR %%%lu", instr->dest.val, instr->p1.src1.val);
            break;
        }
        case IR_INST_TY__FN_CALL_VOID: {
            const SV fn_name = SV_from_identifiermap_lookup(identifier_map, instr->p1.name);
            printf("CALL <"SV_format"> (", SV_v_args(fn_name));
            for (size_t i = 0; i < instr->p2.array->len; i++) {
                printf("%%%lu", instr->p2.array->array[i].val);
                if (i+1 != instr->p2.array->len) {
                    printf(", ");
                }
            }
            putc(')' ,stdout);
            break;
        }
        case IR_INST_TY__FN_CALL_RET: {
            const SV fn_name = SV_from_identifiermap_lookup(identifier_map, instr->p1.name);
            printf("%%%lu = CALL <"SV_format"> (", instr->dest.val, SV_v_args(fn_name));
            for (size_t i = 0; i < instr->p2.array->len; i++) {
                printf("%%%lu", instr->p2.array->array[i].val);
                if (i+1 != instr->p2.array->len) {
                    printf(", ");
                }
            }
            putc(')' ,stdout);
            break;
        }
    }
}

