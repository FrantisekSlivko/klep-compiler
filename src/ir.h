#ifndef KLEPCOMPILER_IR_H
#define KLEPCOMPILER_IR_H
#include <stdint.h>
#include <stdio.h>
#include "sv_u64_map.h"
#include "parser.h"
#include "utils/float64.h"

typedef uint64_t Identifier;

// init_list_type(Identifier, Identifier_LIST)

typedef enum {
    IR_TY_general,
    IR_TY_Ref,
    IR_TY_Array,
    IR_TY_List,
} IR_typeuse_type;

typedef struct IR_typeuse {
    Identifier name;
    IR_typeuse_type tt_ty;
    struct IR_typeuse* contained;
} IR_typeuse;

typedef struct {
    IR_typeuse type;
    Identifier name;
} IR_typename_pair;

init_list_type(IR_typename_pair, IR_typename_pair_LIST)


typedef struct {
    uint64_t val;
} IR_temporal;

init_list_type(IR_temporal, IR_temporal_LIST)

typedef struct {int a;} IR_struct_typedef;
typedef struct {int a;} IR_enum_typedef;

typedef struct {
    IR_temporal temp;
    size_t block;
} IR_temporal_block_phi_record;

init_list_type(IR_temporal_block_phi_record, IR_temporal_block_phi_record_LIST)

typedef enum {
    IR_INST_TY__INVALID = 0,

    IR_INST_TY__PHI,

    // IR_INST_TY__STORE_VAR,
    IR_INST_TY__VAR_DECL,
    // IR_INST_TY__LOAD_VAR,

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
    IR_INST_TY__OP_CMP_LESS,
    IR_INST_TY__OP_CMP_LESS_EQ,
    IR_INST_TY__OP_CMP_GREATER,
    IR_INST_TY__OP_CMP_GREATER_EQ,

    IR_INST_TY__BRANCH,
    IR_INST_TY__JUMP,

    IR_INST_TY__RETURN_VOID,
    IR_INST_TY__RETURN,

    /// dest = GET_FIELD p1.name={field_name}, p2.src2={source}
    IR_INST_TY__GET_FIELD,

    /// dest = INDEX p1.src1={array}, p2.src2={index}
    IR_INST_TY__INDEX,
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
        size_t br_false;
    } p2;
} IR_instr;

init_list_type(IR_instr, IR_instr_LIST)

typedef struct {
    Identifier variable;
    IR_temporal bind;
} IR_SSA_binding;

init_list_type(IR_SSA_binding, IR_SSA_binding_LIST)

typedef struct {
    IR_instr_LIST instructions;
    IR_SSA_binding_LIST bindings;
} IR_block;

init_list_type(IR_block, IR_block_LIST)

// typedef struct {
//     IR_block_LIST blocks;
// } IR_code_section;

typedef struct {
    IR_typeuse fn_ret_t;
    Identifier fn_name;
    IR_typename_pair_LIST args;
    IR_block_LIST blocks;
} IR_func;

init_list_type(IR_func, IR_func_LIST)

void print_IR_instr(const IR_instr* instr, khash_t(sv_u64_map)* identifier_map);


#endif //KLEPCOMPILER_IR_H
