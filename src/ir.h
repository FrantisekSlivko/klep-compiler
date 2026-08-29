#ifndef KLEPCOMPILER_IR_H
#define KLEPCOMPILER_IR_H
#include "ir_defs.h"

#include <stdint.h>
#include <stdio.h>
#include "sv_u64_map.h"
#include "parser.h"
#include "typecompletion.h"
#include "utils/float64.h"



void print_IR_typeuse(const IR_typeuse* restrict ty, khash_t(sv_u64_map)* restrict identifier_map);

typedef struct {
    IR_typeuse type;
    Identifier name;
} IR_typename_pair;

init_prefixed_list_type(IR_typename_pair)


typedef struct {
    uint64_t val;
} IR_temporal;

init_prefixed_list_type(IR_temporal)

typedef struct {
    IR_temporal value;
    Identifier name;
} IR_named_value;

init_prefixed_list_type(IR_named_value)

typedef struct {
    IR_temporal temp;
    size_t block;
} IR_temporal_block_phi_record;

init_prefixed_list_type(IR_temporal_block_phi_record)

typedef enum {
    IR_INST_TY__INVALID = 0,

    IR_INST_TY__PHI,

    // IR_INST_TY__STORE_VAR,
    IR_INST_TY__VAR_DECL,

    /// STORE dest={ptr}, p1.src1={value}
    IR_INST_TY__STORE_PTR,

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

    IR_INST_TY__BRANCH,
    IR_INST_TY__JUMP,

    IR_INST_TY__RETURN_VOID,
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

typedef struct {
    IR_instr_TY ty;
    IR_temporal dest; // used as SRC in BRANCH
    union {
        IR_temporal src1;
        int64_t value;
        float64_t value_float;
        Identifier name;
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
    Identifier variable;
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
    Identifier fn_name;
    IR_typename_pair_LIST args;
    IR_block_LIST blocks;
    size_t highest_temporal_number;
} IR_func;

init_prefixed_list_type(IR_func)




void print_IR_instr(const IR_instr* instr, khash_t(sv_u64_map)* identifier_map);


#endif //KLEPCOMPILER_IR_H
