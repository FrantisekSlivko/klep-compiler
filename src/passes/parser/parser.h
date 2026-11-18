#ifndef KLEPCOMPILER_PARSER_H
#define KLEPCOMPILER_PARSER_H

#include "types.h"
#include "source_info.h"
#include "target.h"
#include "typecompletion.h"

/// bool
#define Token_cmp(t,val) SV_pv_cmp_eq((t), val, sizeof(val)-1)



SV SV_from_identifiermap_lookup(const khash_t(sv_u64_map)* restrict identifier_map, uint64_t identifier);


bool P_has_next(Parser* p);

Token P_next_token(Parser* p);

Token P_peek_token(Parser* p);

IR_temporal P_new_temporal(Parser* p);

IR_SSA_binding_LIST* P_get_current_block_bindings(const Parser* p);

void P_emit(const Parser* p, const IR_instr instr, const IR_instr_error_info error_info);
void P_emit_to_block(const Parser* p, const size_t block_nth, const IR_instr instr, const IR_instr_error_info error_info);

Parser parse(SVList* restrict tokens,
    SourceInfo source_info,
    // TargetInfo target_info,
    GlobalCompilerState* restrict global_compiler_state
    // khash_t(sv_u64_map)* restrict identifier_map,
    // IR_func_LIST* restrict functions,
    // CFFI_IR_extern_fn_LIST* restrict external_functions
);

Identifier SV_to_identifier(
    khash_t(sv_u64_map)* restrict identifier_map,
    uint64_t* restrict identifier_map_cnt,
    const Token restrict t
);

void P_syntax_error(const Parser* restrict p, const Token restrict t, SV msg);
void P_require_having_next_token(Parser* p, const TokenType t);
Token P_require_next_be(Parser* p, const TokenType t, const SV cmp);
Identifier P_to_identifier(Parser* restrict p, const Token restrict t);
IR_typeuse P_get_typeuse(Parser* p, bool allow_void);

size_t P_get_current_block_index(const Parser* restrict p);
IR_SSA_binding_LIST IR_SSA_bindings_deep_clone(const IR_SSA_binding_LIST* original);
void P_append_block_with_bindings(const Parser* p, const IR_SSA_binding_LIST bindings);
void P_append_block(const Parser* p);
IR_temporal P_new_temporal(Parser* p);

bool P_block_is_terminated(const Parser* p, const size_t block_nth);

IR_SSA_binding_LIST P_PHI_CONDITIONAL_BRANCHING_MERGER(Parser* restrict p, const IR_SSA_binding_LIST* restrict a,
                                                       const size_t a_block_id, const IR_SSA_binding_LIST* restrict b,
                                                       const size_t b_block_id, const TokenSpan trigger);

#endif //KLEPCOMPILER_PARSER_H
