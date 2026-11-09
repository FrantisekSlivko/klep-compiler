#ifndef KLEPCOMPILER_IR_H
#define KLEPCOMPILER_IR_H
#include "ir_defs.h"

#include <stdint.h>
#include <stdio.h>
#include "sv_u64_map.h"
#include "utils/float64.h"



void print_IR_typeuse(const IR_typeuse* restrict ty, const khash_t(sv_u64_map)* restrict identifier_map);

SV format_IR_typeuse(const IR_typeuse* restrict ty, const khash_t(sv_u64_map)* restrict identifier_map);

typedef struct {
    uint64_t val;
} IR_temporal;




typedef enum {
    PAT_LITERAL_INT,    // 10, true
    PAT_LITERAL_FLOAT,  // 3.14
    PAT_BINDING,        // x (new binding)
    PAT_VARIANT,        // Option::Some(sub_pattern)
    PAT_WILDCARD,       // _
} Pattern_Kind;

typedef struct Pattern Pattern;

typedef struct Pattern_LIST {
    Pattern* array;
    size_t cap;
    size_t len;
} Pattern_LIST;

typedef struct {
    SpannedIdentifier enum_name;
    SpannedIdentifier variant_name;
    Pattern_LIST sub_patterns; // len->0 if no sub_patterns
} Pattern_Variant;

struct Pattern {
    Pattern_Kind kind;
    TokenSpan span;
    union {
        int64_t int_literal_val;
        float64_t float_literal_val;
        struct {
            SpannedIdentifier name;
            IR_temporal temp;
        } binding; // For identifiers like `x`
        Pattern_Variant variant;
    } info;
};

init_prefixed_list_type(Pattern)




typedef struct {
    IR_typeuse type;
    SpannedIdentifier name;
} IR_typename_pair;

init_prefixed_list_type(IR_typename_pair)




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
    IR_IEI_none,
    IR_IEI_math_operation,
    IR_IEI_literal,
    IR_IEI_explicit_cast,
    IR_IEI_make_array,
    IR_IEI_make_enum,
    IR_IEI_function_call,
    IR_IEI_struct_constructor,
    IR_IEI_get_struct_field,
    IR_IEI_index_array,
    IR_IEI_variable_declaration,
    IR_IEI_phi,
    IR_IEI_branch,
    IR_IEI_return_value,
    IR_IEI_store_ptr,
    IR_IEI_move,
    IR_IEI__MATCH_META__target,
    IR_IEI__MATCH_META__match,
    IR_IEI_enum_get_field_ptr,
    IR_IEI_ref_of,
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
            TokenSpan expr;
            TokenSpan whole;
        } explicit_cast;
        struct {
            TokenSpan* items;
            size_t items_cnt;
            TokenSpan whole;
        } make_array;
        struct {
            TokenSpan variant;
            TokenSpan* items;
            size_t items_cnt;
            TokenSpan whole;
        } make_enum;
        struct {
            TokenSpan name;
            TokenSpan* args;
            size_t args_cnt;
            TokenSpan whole;
        } function_call;
        /// names.span are in the instruction itself
        struct {
            TokenSpan* values;
            size_t values_cnt;
            TokenSpan whole;
        } make_struct;
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
            TokenSpan expr;
        } store_ptr;
        struct {
            TokenSpan value;
        } move;
        struct {
            TokenSpan expr;
        } match_meta__target;
        struct {
            TokenSpan target_expr;
            TokenSpan match_kw;
        } match_meta__match;
        struct {
            TokenSpan whole;
        } enum_get_field_ptr;
        struct {
            TokenSpan value;
            bool is_from_match;
            TokenSpan match_kw; // later union if other cases arise from which REF_OF could originate
        } ref_of;

        void* null;
    } info;
} IR_instr_error_info;
init_prefixed_list_type(IR_instr_error_info)

typedef uint64_t IR_instr_error_info_index;

typedef enum {
    IR_INST_TY__INVALID = 0,

    /// .dest={phi}, p2.phi_args={args} where args = IR_temporal_block_phi_record_LIST*
    IR_INST_TY__PHI,

    /// DROP p1.name={var_name}, dest={temporal}, p2.stype={type}
    IR_INST_TY__DROP,

    /// %n{dest} = MOVE %k
    IR_INST_TY__MOVE,

    IR_INST_TY__ASSIGN,

    /// %n{dest} = REF_OF %k
    IR_INST_TY__REF_OF,

    IR_INST_TY__VAR_DECL,

    /// STORE dest={ptr}, p1.src1={value}
    IR_INST_TY__STORE_PTR,

    /// %dest={res} = CAST p1.src1={temporal}, p2.stype={type}
    IR_INST_TY__EXPLICIT_CAST,

    /// dest={res}, p1.value={val}
    IR_INST_TY__CONST_INT,
    IR_INST_TY__CONST_FLOAT,
    IR_INST_TY__CONST_STRING,
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
    IR_INST_TY__GET_STRUCT_FIELD_PTR,

    /// dest = INDEX_PTR p1.src1={array} [ p2.src2={index} ]
    IR_INST_TY__INDEX_PTR,

    /// dest = READ_PTR p1.src1={ptr}
    IR_INST_TY__READ_PTR,

    /// not expected to return value
    /// CALL p1.name={name} (p2.array={args})
    IR_INST_TY__FN_CALL_RETVOID,

    /// expected to return value
    /// dest = CALL p1.name={name} (p2.array={args})
    IR_INST_TY__FN_CALL_RETVAL,

    /// dest = MAKE_STRUCT p1.optional_typed_name={name, type} { .p2.struct_args }
    IR_INST_TY__MAKE_STRUCT,

    /// dest = MAKE_ENUM p1.optional_typed_name={name, type} ( .p2.array )
    IR_INST_TY__MAKE_ENUM,

    IR_INST_TY__MATCH_META__TARGET,
    IR_INST_TY__MATCH_META__MATCH,

    /// gets ptr to one of the values inside a enum variant
    ENUM_GET_FIELD_PTR,

    /// extracts variant number from enum value, use for enum comparison
    IR_INST_TY__ENUM_VARIANT_NUMBER_EXTRACT,

    /// from enum name and variant name -> variant number and size -> gets replaced by CONST_INT
    IR_INST_TY__ENUM_VARIANT_NUMBER_FROM_TYPE
} IR_instr_TY;


typedef struct {
    IR_instr_TY ty;
    /// index per function into array of debug info for each IR_instr
    IR_instr_error_info_index error_info_index;
    union {
        struct {
            IR_temporal dest;
            IR_temporal_block_phi_record_LIST* args;
        } phi;
        struct {
            SpannedIdentifier var_name;
            IR_temporal dest;
            IR_typeuse* type;
        } drop;
        struct {
            IR_temporal dest;
            IR_temporal src;
        } move;
        struct {
            IR_temporal dest;
            // or 0 if null
            IR_temporal old;
            IR_temporal new;
        } assign;
        struct {
            IR_temporal dest;
            IR_temporal src;
        } ref_of;
        struct {
            SpannedIdentifier var_name;
            IR_temporal dest;
            IR_typeuse* type;
        } var_decl;
        struct {
            IR_temporal ptr;
            IR_temporal value;
        } store_ptr;
        struct {
            IR_temporal dest;
            IR_temporal src;
            IR_typeuse* type;
        } explicit_cast;
        struct {
            IR_temporal dest;
            int64_t val;
        } const_int;
        struct {
            IR_temporal dest;
            float64_t val;
        } const_float;
        struct {
            IR_temporal dest;
            Identifier string;
        } const_string;
        struct {
            IR_temporal dest;
            IR_temporal src1;
            IR_temporal src2;
        } math_cmp_boolean_op;
        struct {
            IR_temporal cond;
            size_t branch_true;
            size_t branch_false;
        } branch;
        struct {
            size_t block;
        } jump;
        void* return_void;
        IR_temporal return_value;
        struct {
            IR_temporal dest;
            SpannedIdentifier field_name;
            IR_temporal struct_ptr;
        } get_struct_field_ptr;
        struct {
            IR_temporal dest;
            IR_temporal array_ptr;
            IR_temporal index_val;
        } index_ptr;
        struct {
            IR_temporal dest;
            IR_temporal ptr;
        } read_ptr;
        struct {
            IR_temporal dest;
            SpannedIdentifier name;
            IR_temporal_LIST* args;
        } fn_call_retval;
        struct {
            SpannedIdentifier name;
            IR_temporal_LIST* args;
        } fn_call_retvoid;
        struct {
            IR_temporal dest;
            IR_temporal_LIST* items;
        } make_array;
        struct {
            IR_temporal dest;
            IR_typeuse* type;
            IR_named_value_LIST* args;
        } make_struct;
        struct {
            IR_temporal dest;
            IR_typeuse* type;
            Identifier variant;
            IR_temporal_LIST* args;
        } make_enum;
        struct {
            IR_temporal target;
        } match_meta__target;
        struct {
            IR_temporal target;
            Pattern_LIST patterns;
        } match_meta__match;
        struct {
            IR_temporal dest;
            IR_temporal enum_src;
            SpannedIdentifier enum_name;
            SpannedIdentifier variant_name;
            uint32_t value_nth;
        } enum_get_field_ptr;
        struct {
            IR_temporal dest;
            IR_temporal enum_src;
        } enum_variant_number_extract;
        struct {
            IR_temporal dest;
            SpannedIdentifier enum_name;
            SpannedIdentifier variant_name;
        } enum_variant_number_from_type;
    } info;
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

typedef struct IR_func {
    IR_typeuse fn_ret_t;
    SpannedIdentifier fn_name;
    SV raw_name;
    IR_typename_pair_LIST args;
    IR_block_LIST blocks;
    size_t highest_temporal_number;
    IR_instr_error_info_LIST error_info_list;
} IR_func;

init_prefixed_list_type(IR_func)

typedef struct IR_generic_function {
    IR_func fn;
    SpannedIdentifier_LIST generic_args;
} IR_generic_function;
init_prefixed_list_type(IR_generic_function)

void print_IR_instr(const IR_instr* instr, const khash_t(sv_u64_map)* identifier_map);

#endif //KLEPCOMPILER_IR_H
