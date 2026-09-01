#ifndef KLEPCOMPILER_IR_H
#define KLEPCOMPILER_IR_H
#include "ir_defs.h"

#include <stdint.h>
#include <stdio.h>
#include "sv_u64_map.h"
#include "parser.h"
#include "typecompletion.h"
#include "utils/float64.h"



void print_IR_typeuse(const IR_typeuse* restrict ty, const khash_t(sv_u64_map)* restrict identifier_map);

typedef struct {
    IR_typeuse type;
    SpannedIdentifier name;
} IR_typename_pair;

init_prefixed_list_type(IR_typename_pair)


typedef struct {
    uint64_t val;
} IR_temporal;

init_prefixed_list_type(IR_temporal)

typedef struct {
    IR_temporal value;
    SpannedIdentifier name;
} IR_named_value;

init_prefixed_list_type(IR_named_value)

typedef struct {
    IR_temporal temp;
    size_t block;
} IR_temporal_block_phi_record;

init_prefixed_list_type(IR_temporal_block_phi_record)

typedef enum {
    IR_INST_TY__INVALID = 0,

    /// .dest={phi}, p2.phi_args={args} where args = IR_temporal_block_phi_record_LIST*
    IR_INST_TY__PHI,

    /// DROP p1.name={var_name}, dest={temporal}, p2.stype={type}
    IR_INST_TY__DROP,

    /// %n{dest} = MOVE %k{p1.src1}
    IR_INST_TY__MOVE,

    IR_INST_TY__VAR_DECL,

    /// STORE dest={ptr}, p1.src1={value}
    IR_INST_TY__STORE_PTR,

    /// %dest={res} = CAST p1.src1={temporal}, p2.stype={type}
    IR_INST_TY__EXPLICIT_CAST,

    /// dest={res}, p1.value={val}
    IR_INST_TY__CONST_INT,
    IR_INST_TY__CONST_FLOAT,
    IR_INST_TY__MAKE_ARRAY,

    IR_INST_TY__OP_ADD,
    IR_INST_TY__OP_SUB,
    IR_INST_TY__OP_DIV,
    IR_INST_TY__OP_MUL,
    IR_INST_TY__OP_POW,

    IR_INST_TY__OP_OR,
    IR_INST_TY__OP_AND,

    IR_INST_TY__OP_CMP_EQ,
    IR_INST_TY__OP_CMP_NOT_EQ,
    IR_INST_TY__OP_CMP_LESS,
    IR_INST_TY__OP_CMP_LESS_EQ,
    IR_INST_TY__OP_CMP_GREATER,
    IR_INST_TY__OP_CMP_GREATER_EQ,

    /// .dest = cmp_rs.temp, .p1.br_true, .p2.br_false,
    IR_INST_TY__BRANCH,
    IR_INST_TY__JUMP,

    IR_INST_TY__RETURN_VOID,
    /// .dest={ret},
    IR_INST_TY__RETURN,

    /// dest = GET_FIELD_PTR p1.name={field_name}, p2.src2={source}
    IR_INST_TY__GET_FIELD_PTR,

    /// dest = INDEX_PTR p1.src1={array} [ p2.src2={index} ]
    IR_INST_TY__INDEX_PTR,

    /// dest = READ_PTR p1.src1={ptr}
    IR_INST_TY__READ_PTR,

    /// not expected to return value
    /// CALL p1.name={name} (p2.array={args})
    IR_INST_TY__FN_CALL_VOID,

    /// expected to return value
    /// dest = CALL p1.name={name} (p2.array={args})
    IR_INST_TY__FN_CALL_RET,

    /// dest = MAKE_STRUCT p1.name={name} { .p2.struct_args }
    IR_INST_TY__MAKE_STRUCT,
} IR_instr_TY;

typedef enum {
    IR_IEI_none,
    IR_IEI_math_operation,
    IR_IEI_literal,
    IR_IEI_explicit_cast,
    IR_IEI_make_array,
    IR_IEI_function_call,
    IR_IEI_struct_constructor,
    IR_IEI_get_struct_field,
    IR_IEI_index_array,
    IR_IEI_variable_declaration,
    IR_IEI_phi,
    IR_IEI_branch,
    IR_IEI_return_value,
    IR_IEI_store_ptr,
} IR_instr_error_info_kind;

typedef struct {
    IR_instr_error_info_kind kind;
    union {
        struct {
            TokenSpan v1;
            TokenSpan v2;
            TokenSpan op;
            TokenSpan whole;
        } math_operation;
        TokenSpan literal;
        struct {
            TokenSpan type;
            TokenSpan expr;
            TokenSpan whole;
        } explicit_cast;
        struct {
            TokenSpan* items;
            size_t items_cnt;
            TokenSpan whole;
        } make_array;
        struct {
            TokenSpan name;
            TokenSpan* args;
            size_t args_cnt;
            TokenSpan whole;
        } function_call;
        /// names.span are in the instruction itself
        struct {
            TokenSpan name;
            TokenSpan* values;
            size_t values_cnt;
            TokenSpan whole;
        } struct_constructor;
        struct {
            TokenSpan origin;
            TokenSpan field_name;
            TokenSpan whole;
        } get_struct_field;
        struct {
            TokenSpan origin;
            TokenSpan index_expr;
            TokenSpan whole;
        } index_array;
        struct {
            TokenSpan name;
            TokenSpan type;
            TokenSpan expr;
            TokenSpan whole;
        } variable_declaration;
        struct {
            TokenSpan trigger;
        } phi;
        struct {
            TokenSpan cmp_expr;
        } branch;
        struct {
            TokenSpan expr;
        } return_value;
        struct {
            TokenSpan to;
            TokenSpan ptr;
        } store_ptr;
        void* null;
    } info;
} IR_instr_error_info;
init_prefixed_list_type(IR_instr_error_info)

typedef uint64_t IR_instr_error_info_index;

typedef struct {
    IR_instr_TY ty;
    IR_temporal dest; // used as SRC in BRANCH
    /// index per function into array of debug info for each IR_instr
    IR_instr_error_info_index error_info_index;
    union {
        IR_temporal src1;
        int64_t value;
        float64_t value_float;
        SpannedIdentifier name;
        size_t br_true;
    } p1;
    union {
        IR_temporal src2;
        IR_typeuse* type;
        IR_temporal_LIST* array;
        IR_temporal_block_phi_record_LIST* phi_args;
        IR_named_value_LIST* struct_args;
        size_t br_false;
    } p2;
} IR_instr;

init_prefixed_list_type(IR_instr)

typedef struct {
    SpannedIdentifier variable;
    IR_temporal bind;
} IR_SSA_binding;

init_prefixed_list_type(IR_SSA_binding)

typedef struct {
    IR_instr_LIST instructions;
    IR_SSA_binding_LIST bindings;
} IR_block;

init_prefixed_list_type(IR_block)


typedef struct {
    IR_typeuse fn_ret_t;
    SpannedIdentifier fn_name;
    IR_typename_pair_LIST args;
    IR_block_LIST blocks;
    size_t highest_temporal_number;
    IR_instr_error_info_LIST error_info_list;
} IR_func;

init_prefixed_list_type(IR_func)




void print_IR_instr(const IR_instr* instr, const khash_t(sv_u64_map)* identifier_map);


#endif //KLEPCOMPILER_IR_H
