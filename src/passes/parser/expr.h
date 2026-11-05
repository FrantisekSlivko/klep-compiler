
#ifndef KLEPCOMPILER_EXPR_H
#define KLEPCOMPILER_EXPR_H

#include "types.h"
#include "error.h"

IR_temporal_with_span P_get_expr(Parser* p);
IR_temporal_with_span P_expr_accesors_wrapper(Parser* restrict p, const IR_temporal_with_span origin);

void P_parse_char_literal(const Parser* p, const Token t, int64_t* out_val);

IR_temporal P_variable_lookup(const Parser* restrict p, const Token restrict name, const Identifier name2);

bool EX_is_OP(const Token op);
P_OP EX_token_to_op(const Token op);
float64_t EX_token_to_float(const Parser* p, const Token t);
int64_t EX_token_to_int(const Parser* p, const Token t);

#endif //KLEPCOMPILER_EXPR_H
