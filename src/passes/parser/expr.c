#include "expr.h"
#include "parser.h"


bool EX_is_OP(const Token op) {
    if (Token_cmp(op, "and")) return true;
    if (op->len != 1 && op->len != 2) {
        return false;
    }
    if (op->len == 1) {
        switch (op->start[0]) {
            case '+':
            case '-':
            case '*':
            case '/':
            case '^':
            case '<':
            case '>':
                return true;
            default:
                return false;
        }
    }

    const char c1 = op->start[0];
    const char c2 = op->start[1];
    return
            (c1 == '=' && c2 == '=') ||
            (c1 == '<' && c2 == '=') ||
            (c1 == '>' && c2 == '=') ||
            (c1 == '!' && c2 == '=') ||
            (c1 == 'o' && c2 == 'r');
    UNREACHABLE()
}

P_OP EX_token_to_op(const Token op) {
    if (Token_cmp(op, "and")) return P_OP__AND;

    if (op->len == 1) {
        switch (op->start[0]) {
            case '+':
                return P_OP__ADD;
            case '-':
                return P_OP__SUB;
            case '*':
                return P_OP__MUL;
            case '/':
                return P_OP__DIV;
            case '^':
                return P_OP__POW;
            case '<':
                return P_OP__CMP_LESS;
            case '>':
                return P_OP__CMP_GREATER;
            default:
                INTERNAL_ERROR("invalid OP '%c' (%u)", op->start[0], op->start[0])
        }
    }
    if (op->len == 2) {
        const char c1 = op->start[0];
        const char c2 = op->start[1];
        if (c1 == '=' && c2 == '=') {
            return P_OP__CMP_EQ;
        }
        if (c1 == '!' && c2 == '=') {
            return P_OP__CMP_NOT_EQ;
        }
        if (c1 == '<' && c2 == '=') {
            return P_OP__CMP_LESS_EQ;
        }
        if (c1 == '>' && c2 == '=') {
            return P_OP__CMP_GREATER_EQ;
        }
        if (c1 == 'o' && c2 == 'r') {
            return P_OP__OR;
        }
    }
    INTERNAL_ERROR("invalid OP '" SV_format "'", SV_p_args(op))
}

uint8_t EX_precedence(const P_OP op) {
    switch (op) {
        case P_OP__OR:
            return 10;
        case P_OP__AND:
            return 15;
        case P_OP__CMP_EQ:
            return 20;
        case P_OP__CMP_NOT_EQ:
            return 21;

        case P_OP__CMP_LESS:
        case P_OP__CMP_LESS_EQ:
        case P_OP__CMP_GREATER:
        case P_OP__CMP_GREATER_EQ:
            return 25;

        case P_OP__ADD:
        case P_OP__SUB:
            return 30;

        case P_OP__MUL:
        case P_OP__DIV:
            return 35;

        case P_OP__POW:
            return 40;
    }
    UNREACHABLE()
}

bool EX_is_op_right_assoc(const P_OP op) {
    return op == P_OP__POW; // ^
}


IR_temporal_with_span P_expr_accesors_wrapper(Parser* restrict p, const IR_temporal_with_span origin) {
    P_require_having_next_token(p, TT_Semicolon);
    const Token nx = P_peek_token(p);
    // const TokenSpan nx_span = (TokenSpan){*nx};

    if (nx->len == 1 && nx->start[0] == '.') {
        P_next_token(p); // dot
        if (!P_has_next(p)) {
            expected_token_error(&p->source, &p->source_filename, nx, "field or function name");
            exit(1);
        }
        const Token field_name = P_next_token(p);
        const TokenSpan field_name_span = (TokenSpan){*field_name};
        const Identifier i = P_to_identifier(p, field_name);

        if (P_has_next(p) && Token_cmp(P_peek_token(p), "(")) {
            P_next_token(p); // (

            IR_temporal_LIST* args = malloc(sizeof(IR_temporal_LIST));
            *args = IR_temporal_LIST_new();
            TokenSpan_LIST args_spans = TokenSpan_LIST_new();

            // prepend the receiver (origin) as the first argument (as self)
            IR_temporal_LIST_push(args, origin.temp);
            TokenSpan_LIST_push(&args_spans, origin.span);

            SV fn_end;
            while (1) {
                P_require_having_next_token(p, TT_EndBracket);
                const Token kx = P_peek_token(p);
                if (Token_cmp(kx, ")")) {
                    P_next_token(p);
                    fn_end = *kx;
                    break;
                }

                const IR_temporal_with_span expr = P_get_expr(p);
                IR_temporal_LIST_push(args, expr.temp);
                TokenSpan_LIST_push(&args_spans, expr.span);

                P_require_having_next_token(p, TT_EndBracket);
                const Token jx = P_next_token(p);
                if (Token_cmp(jx, ")")) {
                    fn_end = *jx;
                    break;
                }

                if (!Token_cmp(jx, ",")) {
                    P_syntax_error(p, jx, SV_from_str("expected either ',' or ')'"));
                    exit(1);
                }
            }

            const IR_temporal fn_call = P_new_temporal(p);
            TokenSpan_LIST_shrink(&args_spans);
            const TokenSpan whole = TS_merge(&origin.span, &(TokenSpan){fn_end});

            P_emit(p, (IR_instr){
                       .ty = IR_INST_TY__FN_CALL_RETVAL,
                       .info.fn_call_retval = {
                           .dest = fn_call,
                           .name = {i, field_name_span},
                           .args = args,
                       }
                   }, (IR_instr_error_info){
                       .kind = IR_IEI_function_call,
                       .info.function_call = {
                           .name = field_name_span,
                           .args = args_spans.array,
                           .args_cnt = args_spans.len,
                           .whole = whole,
                       }
                   });

            return (IR_temporal_with_span) {
                .temp = fn_call,
                .span = whole
            };
            // return P_expr_accesors_wrapper(p, (IR_temporal_with_span){fn_call, whole});
        } else {
            const IR_temporal result = P_new_temporal(p);
            const TokenSpan whole = TS_merge(&origin.span, &field_name_span);

            P_emit(p, (IR_instr){
                       .ty = IR_INST_TY__GET_STRUCT_FIELD_PTR,
                       .info.get_struct_field_ptr = {
                           .dest = result,
                           .field_name = {i, (TokenSpan){*field_name}},
                           .struct_ptr = origin.temp
                       }
                   }, (IR_instr_error_info){
                       .kind = IR_IEI_get_struct_field,
                       .info.get_struct_field = {
                           .field_name = field_name_span,
                           .origin = origin.span,
                           .whole = whole,
                       }
                   });

            return P_expr_accesors_wrapper(p, (IR_temporal_with_span){result, whole});
        }
    }

    if (nx->len == 1 && nx->start[0] == '[') {
        P_next_token(p); // [
        const IR_temporal_with_span index = P_get_expr(p);
        const Token end = P_require_next_be(p, TT_EndSquareBracket, SV_from_str("]"));
        const TokenSpan end_span = (TokenSpan){*end};

        const IR_temporal result = P_new_temporal(p);
        const TokenSpan whole = TS_merge(&origin.span, &end_span);

        P_emit(p, (IR_instr){
                   .ty = IR_INST_TY__INDEX_PTR,
                   .info.index_ptr = {
                       .dest = result,
                       .array_ptr = origin.temp,
                       .index_val = index.temp
                   }
               }, (IR_instr_error_info){
                   .kind = IR_IEI_index_array,
                   .info.index_array = {
                       .origin = origin.span,
                       .index_expr = index.span,
                       .whole = whole
                   }
               });

        return P_expr_accesors_wrapper(p, (IR_temporal_with_span){result, whole});
    }


    return origin;
}

IR_temporal P_variable_lookup(const Parser* restrict p, const Token restrict name, const Identifier name2) {
    // IR_temporal dest = {.val = 0};
    // bool found_temp = false;
    // lookup bindings
    const IR_SSA_binding_LIST* bindings = P_get_current_block_bindings(p);
    for (size_t n = 0; n < bindings->len; n++) {
        if (bindings->array[n].variable.identifier == name2) {
            // found_temp = true;
            return bindings->array[n].bind;
            // dest = bindings->array[n].bind;
        }
    }
    // lookup scope if not in bindings
    // if (!found_temp) {
    P_syntax_error(p, name, SV_from_str("unknown variable"));
    exit(1);
    // }
    // return dest;
}

IR_typeuse P_variable_type_lookup(const Parser* restrict p, const Token restrict name, const Identifier name2) {
    for (size_t n = 0; n < p->compilation_scope.len; n++) {
        const P_compilation_scope cs = p->compilation_scope.array[n];
        for (size_t k = 0; k < cs.variables.len; k++) {
            if (cs.variables.array[k].name.identifier == name2) {
                return cs.variables.array[k].type;
            }
        }
    }
    P_syntax_error(p, name, SV_from_str("unknown variable"));
    exit(1);
}

void P_parse_char_literal(const Parser* p, const Token t, int64_t* out_val) {
    // char literal -> const_int Uint8
    size_t i = 1;
    uint8_t result_val = 0;
    bool is_valid = true;

    if (i >= t->len || t->start[i] == '\'') {
        is_valid = false; // Reject empty literal '' or unexpected EOF
    } else if (t->start[i] == '\\') {
        i++; // Consume the backslash
        if (i >= t->len) {
            is_valid = false;
        } else {
            char c = t->start[i];
            switch (c) {
                case 'n': result_val = '\n';
                    i++;
                    break;
                case 't': result_val = '\t';
                    i++;
                    break;
                case 'r': result_val = '\r';
                    i++;
                    break;
                case 'b': result_val = '\b';
                    i++;
                    break;
                case 'f': result_val = '\f';
                    i++;
                    break;
                case 'v': result_val = '\v';
                    i++;
                    break;
                case 'a': result_val = '\a';
                    i++;
                    break;
                case '\\': result_val = '\\';
                    i++;
                    break;
                case '\'': result_val = '\'';
                    i++;
                    break;
                case '\"': result_val = '\"';
                    i++;
                    break;
                case '?': result_val = '\?';
                    i++;
                    break;
                case 'x': {
                    i++; // Consume 'x'
                    if (i >= t->len || !isxdigit((unsigned char) t->start[i])) {
                        P_syntax_error(p, t, SV_from_str("\\x must be followed by at least one hex digit"));
                        exit(1);
                    } else {
                        uint32_t hex_val = 0;
                        while (i < t->len && isxdigit((unsigned char) t->start[i])) {
                            char hc = t->start[i];
                            int nibble = (hc >= '0' && hc <= '9')
                                             ? (hc - '0')
                                             : (tolower((unsigned char) hc) - 'a' + 10);

                            hex_val = (hex_val << 4) | nibble;
                            if (hex_val > 255) {
                                P_syntax_error(p, t, SV_from_str("hex escape sequence out of range"));
                                exit(1);
                            }
                            i++;
                        }
                        result_val = (uint8_t) hex_val;
                    }
                    break;
                }
                default: {
                    if (c >= '0' && c <= '7') {
                        uint32_t oct_val = 0;
                        int count = 0;
                        // Octal escapes can be up to 3 digits long
                        while (i < t->len && count < 3 && t->start[i] >= '0' && t->start[i] <= '7') {
                            oct_val = (oct_val << 3) | (t->start[i] - '0');
                            if (oct_val > 255) {
                                P_syntax_error(p, t, SV_from_str("octal escape sequence out of range"));
                                exit(1);
                            }
                            i++;
                            count++;
                        }
                        if (is_valid) result_val = (uint8_t) oct_val;
                    } else {
                        is_valid = false; // Reject unknown escape sequences
                    }
                    break;
                }
            }
        }
    } else {
        // Standard ASCII character
        result_val = (uint8_t) t->start[i];
        i++;
    }

    // Crucial Check: Ensure the very next character is the closing quote.
    // This rejects multi-character literals like 'ab' and unclosed quotes.
    if (is_valid) {
        if (i < t->len && t->start[i] == '\'') {
            i++; // Consume the closing quote
        } else {
            is_valid = false;
        }
    }

    if (is_valid) {
        *out_val = result_val;
    } else {
        P_syntax_error(p, t, SV_from_str_len("invalid char literal", 20));
        exit(1);
    }
}


/// handles NUM, VAR, (EXPR), [EXPR, EXPR, ...], FN_CALL(), MAKE_STRUCT {...}
/// {VAR, (EXPR), FN_CALL()}.accessors
IR_temporal_with_span P_get_expr_val(Parser* p) {
    const Token t = P_next_token(p);

    if (Token_cmp(t, "true") || Token_cmp(t, "false")) {
        const IR_temporal rs = P_new_temporal(p);
        P_emit(p, (IR_instr){
                   .ty = IR_INST_TY__CONST_INT,
                   .info.const_int = {
                       .dest = rs,
                       .val = Token_cmp(t, "true") ? 1 : 0,
                   }
               }, (IR_instr_error_info){
                   .kind = IR_IEI_literal,
                   .info.literal = {*t}
               });
        return (IR_temporal_with_span){rs, {*t}};
    }

    if (Token_cmp(t, "cast")) {
        const Token start = P_require_next_be(p, TT_Less, SV_from_str_len("<", 1));
        const IR_typeuse type = P_get_typeuse(p, false);
        P_require_next_be(p, TT_Greater, SV_from_str_len(">", 1));

        P_require_next_be(p, TT_StartBracket, SV_from_str_len("(", 1));
        const IR_temporal_with_span source = P_get_expr(p);
        const Token end = P_require_next_be(p, TT_EndBracket, SV_from_str_len(")", 1));

        const IR_temporal dest = P_new_temporal(p);

        IR_typeuse* ttype = malloc(sizeof(IR_typeuse));
        *ttype = type;

        const TokenSpan whole = TS_merge(&(TokenSpan){*t}, &(TokenSpan){*end});
        P_emit(p, (IR_instr){
                   .ty = IR_INST_TY__EXPLICIT_CAST,
                   .info.explicit_cast = {
                       .dest = dest,
                       .type = ttype,
                       .src = source.temp
                   }
               }, (IR_instr_error_info){
                   .kind = IR_IEI_explicit_cast,
                   .info.explicit_cast = {
                       .expr = source.span,
                       .whole = whole
                   }
               });


        const IR_temporal_with_span wrapped_result = P_expr_accesors_wrapper(p, (IR_temporal_with_span){dest, whole});
        // if (wrapped_result.temp.val != dest.val && out_requires_read_ptr) {
        //     const IR_temporal val_load = P_new_temporal(p);
        //     P_emit(p, (IR_instr){
        //                .ty = IR_INST_TY__READ_PTR,
        //                .info.read_ptr = {
        //                    .dest = val_load,
        //                    .ptr = wrapped_result.temp
        //                }
        //            }, (IR_instr_error_info){.kind = IR_IEI_none});
        //     return (IR_temporal_with_span){val_load, wrapped_result.span};
        // }
        return wrapped_result;
    }

    if (Token_cmp(t, "new")) {
        P_require_having_next_token(p, TT_Identifier);
        // new Struct or new Enum

        IR_typeuse* type = malloc(sizeof(IR_typeuse));
        const SV type_sv_start = *P_peek_token(p);
        *type = P_get_typeuse(p, false);

        // now '{' for struct, or '::' for enum
        P_require_having_next_token(p, TT_StartCurlyBracket);
        Token t1 = P_next_token(p);


        // now '::' for enum or '{' for struct
        if (Token_cmp(t1, "::")) {
            // enum
            P_require_having_next_token(p, TT_Identifier);
            const Token raw_variant_name = P_next_token(p);
            const Identifier variant_name = P_to_identifier(p, raw_variant_name);

            P_require_having_next_token(p, TT_StartBracket);
            const Token nx = P_peek_token(p);

            IR_temporal_LIST* args = malloc(sizeof(IR_temporal_LIST));
            *args = IR_temporal_LIST_new();

            TokenSpan_LIST* args_spans = malloc(sizeof(TokenSpan_LIST));
            *args_spans = TokenSpan_LIST_new();

            if (!Token_cmp(nx, "(")) {
                const IR_temporal rs = P_new_temporal(p);
                const TokenSpan whole = TS_merge(&(TokenSpan){.sv=*t}, &(TokenSpan){*raw_variant_name});
                TokenSpan_LIST_shrink(args_spans);
                P_emit(p, (IR_instr){
                           .ty = IR_INST_TY__MAKE_ENUM,
                           .info.make_enum = {
                               .dest = rs,
                               .type = type,
                               .variant = variant_name,
                               .args = args
                           }
                       }, (IR_instr_error_info){
                           .kind = IR_IEI_make_enum,
                           .info.make_enum = {
                               .variant = (TokenSpan){*raw_variant_name},
                               .items = args_spans->array,
                               .items_cnt = args_spans->len,
                               .whole = whole
                           }
                       });
                return (IR_temporal_with_span){rs, whole};
            } else {
                P_next_token(p); // (
                // get args + )

                TokenSpan end_span;
                while (1) {
                    P_require_having_next_token(p, TT_EndBracket);
                    const Token kx = P_peek_token(p);
                    if (Token_cmp(kx, ")")) {
                        P_next_token(p);
                        end_span = (TokenSpan){*kx};
                        break;
                    }

                    const IR_temporal_with_span expr = P_get_expr(p);
                    IR_temporal_LIST_push(args, expr.temp);
                    TokenSpan_LIST_push(args_spans, expr.span);

                    P_require_having_next_token(p, TT_EndBracket);
                    const Token jx = P_next_token(p);
                    if (Token_cmp(jx, ")")) {
                        end_span = (TokenSpan){*jx};
                        break;
                    }

                    if (!Token_cmp(jx, ",")) {
                        P_syntax_error(p, jx, SV_from_str("expected either ',' or ')'"));
                        exit(1);
                    }
                }

                const TokenSpan whole = TS_merge(&(TokenSpan){.sv=*t}, &end_span);

                TokenSpan_LIST_shrink(args_spans);
                const IR_temporal rs = P_new_temporal(p);
                P_emit(p, (IR_instr){
                           .ty = IR_INST_TY__MAKE_ENUM,
                           .info.make_enum = {
                               .dest = rs,
                               .type = type,
                               .variant = variant_name,
                               .args = args
                           }
                       }, (IR_instr_error_info){
                           .kind = IR_IEI_make_enum,
                           .info.make_enum = {
                               .variant = (TokenSpan){*raw_variant_name},
                               .items = args_spans->array,
                               .items_cnt = args_spans->len,
                               .whole = whole
                           }
                       });
                return (IR_temporal_with_span){rs, whole};
            }
        } else if (Token_cmp(t1, "{")) {
            // struct
            IR_named_value_LIST* args = malloc(sizeof(IR_named_value_LIST));
            *args = IR_named_value_LIST_new();

            TokenSpan_LIST args_values_spans = TokenSpan_LIST_new();

            SV end_span;
            while (1) {
                P_require_having_next_token(p, TT_EndCurlyBracket);
                const Token kx = P_next_token(p);
                if (Token_cmp(kx, "}")) {
                    end_span = *kx;
                    break;
                }
                const Identifier field_name = P_to_identifier(p, kx);
                P_require_next_be(p, TT_Equal, SV_from_str_len("=", 1));
                const IR_temporal_with_span val = P_get_expr(p);

                IR_named_value_LIST_push(args, (IR_named_value){
                                             .name = {field_name, (TokenSpan){*kx}},
                                             .value = val.temp,
                                         });
                TokenSpan_LIST_push(&args_values_spans, val.span);

                P_require_having_next_token(p, TT_EndCurlyBracket);
                const Token jx = P_next_token(p);
                if (Token_cmp(jx, "}")) {
                    end_span = *jx;
                    break;
                }
                if (!Token_cmp(jx, ",")) {
                    P_syntax_error(p, jx, SV_from_str("expected either ',' or '}'"));
                    exit(1);
                }
            }

            const IR_temporal out = P_new_temporal(p);

            TokenSpan_LIST_shrink(&args_values_spans);
            // const TokenSpan whole = TS_merge(&type->full_typeuse_span, &(TokenSpan){end_span});
            const TokenSpan whole = TS_merge(&(TokenSpan){ type_sv_start }, &(TokenSpan){end_span});
            P_emit(p, (IR_instr){
                       .ty = IR_INST_TY__MAKE_STRUCT,
                       .info.make_struct = {
                           .dest = out,
                           .type = type,
                           .args = args
                       }
                   }, (IR_instr_error_info){
                       .kind = IR_IEI_struct_constructor,
                       .info.make_struct = {
                           .values = args_values_spans.array,
                           .values_cnt = args_values_spans.len,
                           .whole = whole,
                       }
                   });
            return (IR_temporal_with_span){out, whole};
        } else {
            // error
            P_syntax_error(p, t1, SV_from_str("expected either '{' or \"::\""));
            exit(1);
        }
    }

    if (Token_cmp(t, "(")) {
        // (EXPR)
        const IR_temporal_with_span rs = P_get_expr(p);
        P_require_next_be(p, TT_EndBracket, SV_from_str(")"));

        // bool out_requires_read_ptr = true;
        const IR_temporal_with_span wrapped_result = P_expr_accesors_wrapper(p, rs);
        // if (wrapped_result.temp.val != rs.temp.val && out_requires_read_ptr) {
        //     const IR_temporal val_load = P_new_temporal(p);
        //     P_emit(p, (IR_instr){
        //                .ty = IR_INST_TY__READ_PTR,
        //                .info.read_ptr = {
        //                    .dest = val_load,
        //                    .ptr = wrapped_result.temp
        //                }
        //            }, (IR_instr_error_info){.kind = IR_IEI_none});
        //     return (IR_temporal_with_span){val_load, wrapped_result.span};
        // }
        return wrapped_result;
    }

    if (Token_cmp(t, "[")) {
        // array
        P_require_having_next_token(p, TT_EndSquareBracket);
        const Token t1 = P_peek_token(p);

        IR_temporal_LIST* items = malloc(sizeof(IR_temporal_LIST));
        *items = IR_temporal_LIST_new();

        TokenSpan_LIST itemss_spans = TokenSpan_LIST_new();


        if (Token_cmp(t1, "]")) {
            P_next_token(p);
            const IR_temporal dest = P_new_temporal(p);
            const TokenSpan whole = TS_merge(&(TokenSpan){*t}, &(TokenSpan){*t1});
            P_emit(p, (IR_instr){
                       .ty = IR_INST_TY__MAKE_ARRAY,
                       .info.make_array = {
                           .dest = dest,
                           .items = items
                       }
                   }, (IR_instr_error_info){
                       .kind = IR_IEI_make_array,
                       .info.make_array = {
                           .items = NULL,
                           .items_cnt = 0,
                           .whole = whole
                       }
                   });
            return (IR_temporal_with_span){dest, whole};
        }

        SV end_s;
        while (1) {
            P_require_having_next_token(p, TT_EndSquareBracket);
            const Token tok = P_peek_token(p);
            if (Token_cmp(tok, "]")) {
                P_next_token(p);
                end_s = *tok;
                break;
            }

            const IR_temporal_with_span expr = P_get_expr(p);
            IR_temporal_LIST_push(items, expr.temp);
            TokenSpan_LIST_push(&itemss_spans, expr.span);

            P_require_having_next_token(p, TT_EndSquareBracket);
            const Token end = P_next_token(p);
            if (Token_cmp(end, "]")) {
                end_s = *end;
                break;
            }

            if (Token_cmp(end, ",")) {
                continue;
            }
            P_syntax_error(p, end, SV_from_str("expected ',' or ']'"));
        }

        const IR_temporal dest = P_new_temporal(p);

        TokenSpan_LIST_shrink(&itemss_spans);

        const TokenSpan whole = TS_merge(&(TokenSpan){*t}, &(TokenSpan){end_s});
        P_emit(p, (IR_instr){
                   .ty = IR_INST_TY__MAKE_ARRAY,
                   .info.make_array = {
                       .dest = dest,
                       .items = items
                   }
               }, (IR_instr_error_info){
                   .kind = IR_IEI_make_array,
                   .info.make_array = {
                       .items = itemss_spans.array,
                       .items_cnt = itemss_spans.len,
                       .whole = whole
                   }
               });
        return (IR_temporal_with_span){dest, whole};
    }

    if (t->start[0] == '"') {
        char* unescaped = malloc(t->len);

        size_t out_len = 0;
        size_t i = 1;
        bool is_closed = false;

        while (i < t->len) {
            if (t->start[i] == '"') {
                is_closed = true;
                i++; // Consume the closing quote
                break;
            }

            if (t->start[i] == '\\') {
                i++; // Consume backslash
                if (i >= t->len) {
                    free(unescaped);
                    P_syntax_error(p, t, SV_from_str("unexpected EOF in escape sequence"));
                    exit(1);
                }

                char c = t->start[i];
                switch (c) {
                    case 'n': unescaped[out_len++] = '\n';
                        i++;
                        break;
                    case 't': unescaped[out_len++] = '\t';
                        i++;
                        break;
                    case 'r': unescaped[out_len++] = '\r';
                        i++;
                        break;
                    case 'b': unescaped[out_len++] = '\b';
                        i++;
                        break;
                    case 'f': unescaped[out_len++] = '\f';
                        i++;
                        break;
                    case 'v': unescaped[out_len++] = '\v';
                        i++;
                        break;
                    case 'a': unescaped[out_len++] = '\a';
                        i++;
                        break;
                    case '\\': unescaped[out_len++] = '\\';
                        i++;
                        break;
                    case '\'': unescaped[out_len++] = '\'';
                        i++;
                        break;
                    case '\"': unescaped[out_len++] = '\"';
                        i++;
                        break;
                    case '?': unescaped[out_len++] = '\?';
                        i++;
                        break;
                    case 'x': {
                        i++;
                        if (i >= t->len || !isxdigit((unsigned char)t->start[i])) {
                            P_syntax_error(p, t, SV_from_str("\\x must be followed by at least one hex digit"));
                            exit(1);
                        }
                        uint32_t hex_val = 0;
                        while (i < t->len && isxdigit((unsigned char)t->start[i])) {
                            char hc = t->start[i];
                            int nibble = (hc >= '0' && hc <= '9')
                                             ? (hc - '0')
                                             : (tolower((unsigned char) hc) - 'a' + 10);

                            hex_val = (hex_val << 4) | nibble;
                            if (hex_val > 255) {
                                P_syntax_error(p, t, SV_from_str("hex escape sequence out of range"));
                                exit(1);
                            }
                            i++;
                        }
                        unescaped[out_len++] = (char) (uint8_t) hex_val;
                        break;
                    }
                    default: {
                        if (c >= '0' && c <= '7') {
                            uint32_t oct_val = 0;
                            int count = 0;
                            while (i < t->len && count < 3 && t->start[i] >= '0' && t->start[i] <= '7') {
                                oct_val = (oct_val << 3) | (t->start[i] - '0');
                                if (oct_val > 255) {
                                    P_syntax_error(p, t, SV_from_str("octal escape sequence out of range"));
                                    exit(1);
                                }
                                i++;
                                count++;
                            }
                            unescaped[out_len++] = (char) (uint8_t) oct_val;
                        } else {
                            P_syntax_error(p, t, SV_from_str("unknown escape sequence"));
                            exit(1);
                        }
                        break;
                    }
                }
            } else {
                // Normal character
                unescaped[out_len++] = t->start[i];
                i++;
            }
        }

        if (!is_closed) {
            free(unescaped);
            P_syntax_error(p, t, SV_from_str("unterminated string literal"));
            exit(1);
        }

        // Pass the decoded string to the identifier pool
        const SV raw_str = (SV){.start = unescaped, .len = out_len};
        const Identifier str = P_to_identifier(p, &raw_str);

        const IR_temporal dest = P_new_temporal(p);
        P_emit(p, (IR_instr){
                   .ty = IR_INST_TY__CONST_STRING,
                   .info.const_string = {
                       .dest = dest,
                       .string = str
                   }
               }, (IR_instr_error_info){
                   .kind = IR_IEI_literal,
                   .info.literal = (TokenSpan){.sv = *t}
               });

        return (IR_temporal_with_span){dest, (TokenSpan){.sv = *t}};
    }

    if (t->start[0] == '\'') {
        int64_t result_val;
        P_parse_char_literal(p, t, &result_val);

        const IR_temporal res = P_new_temporal(p);
        P_emit(p, (IR_instr){
                   .ty = IR_INST_TY__CONST_INT,
                   .info.const_int = {
                       .dest = res,
                       .val = result_val,
                   }
               }, (IR_instr_error_info){
                   .kind = IR_IEI_literal,
                   .info.literal = (TokenSpan){.sv = *t}
               });

        return (IR_temporal_with_span){res, (TokenSpan){.sv = *t}};
    }

    // name
    if (('a' <= t->start[0] && t->start[0] <= 'z') || ('A' <= t->start[0] && t->start[0] <= 'Z')) {
        // const IR_temporal dest = P_new_temporal(p);

        const Identifier name = P_to_identifier(p, t);
        const TokenSpan name_span = (TokenSpan){*t};

        IR_temporal dest = {0};
        TokenSpan dest_span = {0};

        if (P_has_next(p)) {
            const Token nx = P_peek_token(p);
            if (Token_cmp(nx, "(")) {
                // fn call
                P_next_token(p); // (

                IR_temporal_LIST* args = malloc(sizeof(IR_temporal_LIST));
                *args = IR_temporal_LIST_new();
                TokenSpan_LIST argss_spans = TokenSpan_LIST_new();

                SV fn_end;

                while (1) {
                    P_require_having_next_token(p, TT_EndBracket);
                    const Token kx = P_peek_token(p);
                    if (Token_cmp(kx, ")")) {
                        P_next_token(p);
                        fn_end = *kx;
                        break;
                    }

                    const IR_temporal_with_span expr = P_get_expr(p);
                    IR_temporal_LIST_push(args, expr.temp);
                    TokenSpan_LIST_push(&argss_spans, expr.span);

                    P_require_having_next_token(p, TT_EndBracket);
                    const Token jx = P_next_token(p);
                    if (Token_cmp(jx, ")")) {
                        fn_end = *jx;
                        break;
                    }

                    if (!Token_cmp(jx, ",")) {
                        P_syntax_error(p, jx, SV_from_str("expected either ',' or ')'"));
                        exit(1);
                    }
                }

                const IR_temporal fn_call = P_new_temporal(p);

                TokenSpan_LIST_shrink(&argss_spans);
                const TokenSpan whole = TS_merge(&name_span, &(TokenSpan){fn_end});
                P_emit(p, (IR_instr){
                           .ty = IR_INST_TY__FN_CALL_RETVAL,
                           .info.fn_call_retval = {
                               .dest = fn_call,
                               .name = {name, (TokenSpan){*t}},
                               .args = args,
                           }
                       }, (IR_instr_error_info){
                           .kind = IR_IEI_function_call,
                           .info.function_call = {
                               .name = name_span,
                               .args = argss_spans.array,
                               .args_cnt = argss_spans.len,
                               .whole = whole,
                           }
                       });

                dest = fn_call;
                dest_span = whole;
            } else {
                dest = P_variable_lookup(p, t, name);
                dest_span = name_span;
            }
        } else {
            dest = P_variable_lookup(p, t, name);
            dest_span = name_span;
        }


        // bool out_requires_read_ptr = true;
        const IR_temporal_with_span wrapped_result = P_expr_accesors_wrapper(p, (IR_temporal_with_span){dest, dest_span});
        // if (wrapped_result.temp.val != dest.val && out_requires_read_ptr) {
        //     const IR_temporal val_load = P_new_temporal(p);
        //     P_emit(p, (IR_instr){
        //                .ty = IR_INST_TY__READ_PTR,
        //                .info.read_ptr = {
        //                    .dest = val_load,
        //                    .ptr = wrapped_result.temp
        //                }
        //            }, (IR_instr_error_info){.kind = IR_IEI_none});
        //     return (IR_temporal_with_span){val_load, wrapped_result.span};
        // }
        return wrapped_result;
    }

    bool is_float = false;
    for (size_t n = 0; n < t->len; n++) {
        if (t->start[n] == '.') {
            if (is_float) {
                P_syntax_error(p, t, SV_from_str("malformed float"));
                exit(1);
            }
            is_float = true;
        }
    }

    const TokenSpan start_span = (TokenSpan){*t};
    TokenSpan res_span = start_span;
    if (is_float) {
        float64_t val;
        if (Token_cmp(t, "-")) {
            P_require_having_next_token(p, TT_Identifier);
            const Token t2 = P_next_token(p);
            res_span = TS_merge(&start_span, &(TokenSpan){*t2});
            val = -EX_token_to_float(p, t2);
        } else {
            val = EX_token_to_float(p, t);
        }

        const IR_temporal res = P_new_temporal(p);

        P_emit(p, (IR_instr){
                   .ty = IR_INST_TY__CONST_FLOAT,
                   .info.const_float = {
                       .dest = res,
                       .val = val
                   }
               }, (IR_instr_error_info){
                   .kind = IR_IEI_literal,
                   .info.literal = res_span
               });

        return (IR_temporal_with_span){res, res_span};
    } else {
        int64_t val;
        if (Token_cmp(t, "-")) {
            P_require_having_next_token(p, TT_Identifier);
            const Token t2 = P_next_token(p);
            res_span = TS_merge(&start_span, &(TokenSpan){*t2});
            val = -EX_token_to_int(p, t2);
        } else {
            val = EX_token_to_int(p, t);
        }

        const IR_temporal res = P_new_temporal(p);

        P_emit(p, (IR_instr){
                   .ty = IR_INST_TY__CONST_INT,
                   .info.const_int = {
                       .dest = res,
                       .val = val,
                   }
               }, (IR_instr_error_info){
                   .kind = IR_IEI_literal,
                   .info.literal = res_span
               });

        return (IR_temporal_with_span){res, res_span};
    }
}

/// returns IR handle to value that the expr produced
IR_temporal_with_span P_get_expr(Parser* p) {
    P_OP_LIST ops = P_OP_LIST_new();
    TokenSpan_LIST ops_spans = TokenSpan_LIST_new();

    IR_temporal_LIST temps = IR_temporal_LIST_new();
    TokenSpan_LIST temps_spans = TokenSpan_LIST_new();

    const IR_temporal_with_span start_val = P_get_expr_val(p);
    IR_temporal_LIST_push(&temps, start_val.temp);
    TokenSpan_LIST_push(&temps_spans, start_val.span);

    bool get_v = false;
    while (1) {
        if (get_v) {
            const IR_temporal_with_span val = P_get_expr_val(p);
            IR_temporal_LIST_push(&temps, val.temp);
            TokenSpan_LIST_push(&temps_spans, val.span);
        } else {
            if (!P_has_next(p)) {
                break;
            }
            const Token t = P_peek_token(p);
            if (t->len == 0) {
                break;
            }

            if (EX_is_OP(t)) {
                // t->len == 1 &&
                P_next_token(p); // advance
                const P_OP item = EX_token_to_op(t);

                while (!P_OP_LIST_is_empty(&ops) &&
                       (EX_precedence(*P_OP_LIST_peek_last(&ops)) > EX_precedence(item)
                        ||
                        (EX_precedence(*P_OP_LIST_peek_last(&ops)) == EX_precedence(item) && !
                         EX_is_op_right_assoc(item))
                       )
                ) {
                    const P_OP op = *P_OP_LIST_pop(&ops);
                    const TokenSpan op_span = *TokenSpan_LIST_pop(&ops_spans);

                    const IR_temporal v1 = *IR_temporal_LIST_pop(&temps);
                    const TokenSpan v1_span = *TokenSpan_LIST_pop(&temps_spans);

                    const IR_temporal v2 = *IR_temporal_LIST_pop(&temps);
                    const TokenSpan v2_span = *TokenSpan_LIST_pop(&temps_spans);

                    const TokenSpan whole = TS_merge(&v1_span, &v2_span);

                    const IR_temporal dest = P_new_temporal(p);

                    IR_instr_TY ty = IR_INST_TY__INVALID;
                    switch (op) {
                        case P_OP__ADD:
                            ty = IR_INST_TY__OP_ADD;
                            break;
                        case P_OP__SUB:
                            ty = IR_INST_TY__OP_SUB;
                            break;
                        case P_OP__DIV:
                            ty = IR_INST_TY__OP_DIV;
                            break;
                        case P_OP__MUL:
                            ty = IR_INST_TY__OP_MUL;
                            break;
                        case P_OP__POW:
                            ty = IR_INST_TY__OP_POW;
                            break;

                        case P_OP__OR:
                            ty = IR_INST_TY__OP_OR;
                            break;
                        case P_OP__AND:
                            ty = IR_INST_TY__OP_AND;
                            break;


                        case P_OP__CMP_EQ:
                            ty = IR_INST_TY__OP_CMP_EQ;
                            break;
                        case P_OP__CMP_NOT_EQ:
                            ty = IR_INST_TY__OP_CMP_NOT_EQ;
                            break;
                        case P_OP__CMP_LESS:
                            ty = IR_INST_TY__OP_CMP_LESS;
                            break;
                        case P_OP__CMP_LESS_EQ:
                            ty = IR_INST_TY__OP_CMP_LESS_EQ;
                            break;
                        case P_OP__CMP_GREATER:
                            ty = IR_INST_TY__OP_CMP_GREATER;
                            break;
                        case P_OP__CMP_GREATER_EQ:
                            ty = IR_INST_TY__OP_CMP_GREATER_EQ;
                            break;
                    }

                    P_emit(p, (IR_instr){
                               .ty = ty,
                               .info.math_cmp_boolean_op = {
                                   .dest = dest,
                                   .src1 = v2,
                                   .src2 = v1
                               }
                           }, (IR_instr_error_info){
                               .kind = IR_IEI_math_operation,
                               .info.math_operation = {
                                   .v1 = v2_span,
                                   .v2 = v1_span,
                                   .op = op_span,
                                   .whole = whole
                               }
                           });

                    IR_temporal_LIST_push(&temps, dest);
                    TokenSpan_LIST_push(&temps_spans, whole);
                }
                P_OP_LIST_push(&ops, item);
                TokenSpan_LIST_push(&ops_spans, (TokenSpan){*t});
            } else {
                break;
            }
        }
        get_v = !get_v;
    }
    while (!P_OP_LIST_is_empty(&ops)) {
        const P_OP op = *P_OP_LIST_pop(&ops);
        const TokenSpan op_span = *TokenSpan_LIST_pop(&ops_spans);

        const IR_temporal v1 = *IR_temporal_LIST_pop(&temps);
        const TokenSpan v1_span = *TokenSpan_LIST_pop(&temps_spans);

        const IR_temporal v2 = *IR_temporal_LIST_pop(&temps);
        const TokenSpan v2_span = *TokenSpan_LIST_pop(&temps_spans);

        const TokenSpan whole = TS_merge(&v1_span, &v2_span);

        const IR_temporal dest = P_new_temporal(p);

        IR_instr_TY ty = IR_INST_TY__INVALID;
        switch (op) {
            case P_OP__ADD:
                ty = IR_INST_TY__OP_ADD;
                break;
            case P_OP__SUB:
                ty = IR_INST_TY__OP_SUB;
                break;
            case P_OP__DIV:
                ty = IR_INST_TY__OP_DIV;
                break;
            case P_OP__MUL:
                ty = IR_INST_TY__OP_MUL;
                break;
            case P_OP__POW:
                ty = IR_INST_TY__OP_POW;
                break;

            case P_OP__OR:
                ty = IR_INST_TY__OP_OR;
                break;
            case P_OP__AND:
                ty = IR_INST_TY__OP_AND;
                break;


            case P_OP__CMP_EQ:
                ty = IR_INST_TY__OP_CMP_EQ;
                break;
            case P_OP__CMP_NOT_EQ:
                ty = IR_INST_TY__OP_CMP_NOT_EQ;
                break;
            case P_OP__CMP_LESS:
                ty = IR_INST_TY__OP_CMP_LESS;
                break;
            case P_OP__CMP_LESS_EQ:
                ty = IR_INST_TY__OP_CMP_LESS_EQ;
                break;
            case P_OP__CMP_GREATER:
                ty = IR_INST_TY__OP_CMP_GREATER;
                break;
            case P_OP__CMP_GREATER_EQ:
                ty = IR_INST_TY__OP_CMP_GREATER_EQ;
                break;
        }

        P_emit(p, (IR_instr){
                   .ty = ty,
                   .info.math_cmp_boolean_op = {
                       .dest = dest,
                       .src1 = v2,
                       .src2 = v1
                   }
               }, (IR_instr_error_info){
                   .kind = IR_IEI_math_operation,
                   .info.math_operation = {
                       .v1 = v2_span,
                       .v2 = v1_span,
                       .op = op_span,
                       .whole = whole
                   }
               });

        IR_temporal_LIST_push(&temps, dest);
        TokenSpan_LIST_push(&temps_spans, whole);
    }

    const IR_temporal last = *IR_temporal_LIST_pop(&temps); // safe since deref
    const TokenSpan last_span = *TokenSpan_LIST_pop(&temps_spans);

    P_OP_LIST_free(&ops);
    TokenSpan_LIST_free(&ops_spans);
    IR_temporal_LIST_free(&temps);
    TokenSpan_LIST_free(&temps_spans);
    return (IR_temporal_with_span){last, last_span};
}
