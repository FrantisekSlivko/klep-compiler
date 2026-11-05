#ifndef KLEPCOMPILER_P_TYPES_H
#define KLEPCOMPILER_P_TYPES_H

#include <stdint.h>
#include <ctype.h>


#include "string_view.h"

#include "ir.h"
#include "khash.h"
#include "sv_u64_map.h"
#include "list.h"


typedef const SV* Token;

/// used for expected errors
typedef enum {
    TT_Identifier,
    TT_Type,
    TT_StartBracket,
    TT_EndBracket,
    TT_Comma,
    TT_StartCurlyBracket,
    TT_EndCurlyBracket,
    TT_Less,
    TT_Greater,
    TT_Semicolon,
    TT_Equal,
    TT_StartSquareBracket,
    TT_EndSquareBracket,
    TT_Star,
    TT_Integer
} TokenType;

typedef struct {
    IR_temporal temp;
    TokenSpan span;
} IR_temporal_with_span;

init_prefixed_list_type(IR_temporal_with_span)

typedef struct {
    Pattern* pat;
    IR_temporal cond_temp;
    /// for wildcards and bindings
    bool is_unconditional;

    size_t pre_branch_block;
    size_t final_eval_block;
    size_t start_block;
    size_t end_block;

    IR_SSA_binding_LIST bindings;

    bool terminated;
} P_match_arm_info;

init_prefixed_list_type(P_match_arm_info)

typedef struct {
    size_t block;
    IR_temporal br_rs;
    size_t case_nth;
    size_t true_block;
} P_match_branch_patch;

init_prefixed_list_type(P_match_branch_patch)


typedef struct {
    SpannedIdentifier name;
    IR_typeuse type;
} P_variable_record;

init_list_type(P_variable_record, P_variable_record_LIST)

typedef struct {
    P_variable_record_LIST variables;
} P_compilation_scope;

init_list_type(P_compilation_scope, P_compilation_scope_LIST)








typedef struct {
    // --- compilation process --
    SVListView positioner;
    const SV source;
    const SV source_filename;


    // global result accumulation
    IR_func_LIST functions;
    khash_t(sv_u64_map)* identifier_map;
    uint64_t identifier_map_cnt;

    // per-function code compilation
    IR_block_LIST* func_scope_blocks;
    IR_instr_error_info_LIST* func_error_info_list;
    uint64_t temporal_fn_cnt;
    P_compilation_scope_LIST compilation_scope;
    IR_typename_pair_LIST* fn_args;

    IR_incomplete_TypeTable type_table;
    CFFI_IR_extern_fn_LIST external_functions;
    IR_generic_function_LIST generic_functions;
} Parser;

typedef enum {
    P_OP__ADD,
    P_OP__SUB,
    P_OP__DIV,
    P_OP__MUL,
    P_OP__POW,

    P_OP__CMP_EQ,
    P_OP__CMP_NOT_EQ,
    P_OP__CMP_LESS,
    P_OP__CMP_LESS_EQ,
    P_OP__CMP_GREATER,
    P_OP__CMP_GREATER_EQ,

    P_OP__OR,
    P_OP__AND,
} P_OP;

init_list_type(P_OP, P_OP_LIST)

#endif //KLEPCOMPILER_P_TYPES_H
