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


TC_TypeTable parse(SVList* restrict tokens,
    SourceInfo source_info,
    TargetInfo target_info,
    khash_t(sv_u64_map)* restrict identifier_map,
    IR_func_LIST* restrict functions,
    CFFI_IR_extern_fn_LIST* restrict external_functions
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

#endif //KLEPCOMPILER_PARSER_H
