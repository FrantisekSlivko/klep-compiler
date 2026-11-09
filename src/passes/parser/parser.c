#include "parser.h"

#include "expr.h"
#include "error.h"
#include "passes/typeinference/typeinference.h"


#define P_require_semicolon() P_require_next_be(p, TT_Semicolon, SV_from_str_len(";", 1));




bool P_has_next(Parser* p) {
    return SVListView_has_next(&p->positioner);
}

Token P_next_token(Parser* p) {
    return SVListView_next(&p->positioner);
}

Token P_peek_token(Parser* p) {
    return SVListView_peek(&p->positioner);
}

void P_get_code_section(Parser* p);


char* TT_to_expected(const TokenType t) {
    switch (t) {
        case TT_Identifier: return "Identifier";
        case TT_Type: return "Type";
        case TT_StartBracket: return "(";
        case TT_EndBracket: return ")";
        case TT_Comma: return ",";
        case TT_StartCurlyBracket: return "{";
        case TT_EndCurlyBracket: return "}";
        case TT_Less: return "<";
        case TT_Greater: return ">";
        case TT_Semicolon: return ";";
        case TT_Equal: return "=";
        case TT_StartSquareBracket: return "[";
        case TT_EndSquareBracket: return "]";
        case TT_Star: return "*";
        case TT_Integer: return "Integer";
    }
    UNREACHABLE();
}

void P_syntax_error(const Parser* restrict p, const Token restrict t, SV msg) {
    syntax_error(&p->source, &p->source_filename, t, &msg);
}

void P_require_having_next_token(Parser* p, const TokenType t) {
    if (P_has_next(p)) return;

    const char* expected = TT_to_expected(t);

    const Token last_token = &p->positioner.ref->array[p->positioner.pos - 1];
    expected_token_error(&p->source, &p->source_filename, last_token, expected);
    exit(1);
}

Token P_require_next_be(Parser* p, const TokenType t, const SV cmp) {
    P_require_having_next_token(p, t);
    const Token sb = P_next_token(p);
    if (!SV_pp_cmp_eq(sb, &cmp)) {
        const char* expected = TT_to_expected(t);
        char* raw_msg = malloc(20);
        snprintf(raw_msg, 20, "expected '%s'", expected);
        const SV msg = SV_from_str(raw_msg);
        P_syntax_error(p, sb, msg);
        free(raw_msg);
        exit(1);
    }
    return sb;
}

// -------------------------------------------


Identifier SV_to_identifier(khash_t(sv_u64_map)* restrict identifier_map, uint64_t* restrict identifier_map_cnt,
                            const Token restrict t) {
    const khiter_t kf = kh_get(sv_u64_map, identifier_map, *t);
    if (kf != kh_end(identifier_map)) {
        return kh_value(identifier_map, kf); // found
    }

    int ret;
    const khiter_t k = kh_put(sv_u64_map, identifier_map, *t, &ret);
    const Identifier id = (*identifier_map_cnt)++;
    kh_value(identifier_map, k) = id;
    return id;
}

Identifier P_to_identifier(Parser* restrict p, const Token restrict t) {
    const char c1 = *t->start;
    if (!(('a' <= c1 && c1 <= 'z') || ('A' <= c1 && c1 <= 'Z'))) {
        const SV msg = SV_from_str("expected 'Identifier'");
        P_syntax_error(p, t, msg);
        exit(1);
    }
    return SV_to_identifier(p->identifier_map, &p->identifier_map_cnt, t);
}


SV SV_from_identifiermap_lookup(const khash_t(sv_u64_map)* identifier_map, const uint64_t identifier) {
    for (khint_t k = kh_begin(identifier_map); k != kh_end(identifier_map); ++k) {
        if (!kh_exist(identifier_map, k)) continue;

        const SV key = kh_key(identifier_map, k);
        const uint64_t val = kh_value(identifier_map, k);
        if (val != identifier) {
            continue;
        }

        return key;
    }
    UNREACHABLE("Identifier lookup");
}


IR_typeuse P_get_typeuse(Parser* p, bool allow_void);

IR_typeuse P_get_typeuse_intern_single_generic(Parser* p, const Token name, const IR_typeuse_type ty,
                                               const bool allow_void) {
    P_require_next_be(p, TT_Less, SV_from_str_len("<", 1));

    IR_typeuse* irt = malloc(sizeof(IR_typeuse));
    *irt = P_get_typeuse(p, allow_void);

    const Token end = P_require_next_be(p, TT_Greater, SV_from_str_len(">", 1));

    const Identifier id_name = P_to_identifier(p, name);

    return (IR_typeuse){
        .info.named = {id_name, (TokenSpan){*name}},
        .tt_ty = ty,
        .contained = irt,
        // .full_typeuse_span = TS_merge(&(TokenSpan){*name}, &(TokenSpan){*end})
    };
}

int64_t EX_token_to_int(const Parser* p, const Token t) {
    int64_t val;
    if (!SV_to_int64_copybased(*t, &val)) {
        P_syntax_error(p, t, SV_from_str("failed to parse as Int"));
        exit(1);
    }
    return val;
}

float64_t EX_token_to_float(const Parser* p, const Token t) {
    float64_t val;
    if (!SV_to_float64_copybased(*t, &val)) {
        P_syntax_error(p, t, SV_from_str("failed to parse as Float"));
        exit(1);
    }
    return val;
}

void default_type_name(const Token name, SV* restrict out) {
    if (Token_cmp(name, "Int")) {
        *out = SV_from_str_len("Int32", 5);
        return;
    }
    if (Token_cmp(name, "Uint")) {
        *out = SV_from_str_len("Uint32", 6);
        return;
    }
    if (Token_cmp(name, "Float")) {
        *out = SV_from_str_len("Float32", 7);
        return;
    }
    *out = *name;
}

IR_typeuse P_get_typeuse(Parser* p, const bool allow_void) {
    const Token name = P_next_token(p);

    if (Token_cmp(name, "Void") || Token_cmp(name, "void")) {
        if (!allow_void) {
            P_syntax_error(p, name, SV_from_str("void as type not allowed here"));
            exit(1);
        }
        return (IR_typeuse){.tt_ty = IR_TY_Void}; // .full_typeuse_span = {*name}
    }

    SV defaulted_type_name;
    default_type_name(name, &defaulted_type_name);

    const Identifier id_name = P_to_identifier(p, &defaulted_type_name);


    TC_Primitive_TY tt;
    if (TC_Primitive_TY_from_SV(&defaulted_type_name, &tt)) {
        return (IR_typeuse){.info.primitive = tt, .tt_ty = IR_TY_Primitive}; // .full_typeuse_span = {*name}
    }

    if (Token_cmp(name, "Ref")) {
        return P_get_typeuse_intern_single_generic(p, name, IR_TY_Ref, false);
    }

    if (Token_cmp(name, "Array")) {
        P_require_next_be(p, TT_Less, SV_from_str_len("<", 1));

        IR_typeuse* irt = malloc(sizeof(IR_typeuse));
        *irt = P_get_typeuse(p, allow_void);

        // P_require_next_be(p, TT_Comma, SV_from_str_len(",", 1));
        P_require_having_next_token(p, TT_Comma);
        {
            const Token comma = P_next_token(p);
            if (!Token_cmp(comma, ",")) {
                P_syntax_error(p, comma, SV_from_str("expected ',' and than integer literal specifying array size"));
                exit(1);
            }
        }

        P_require_having_next_token(p, TT_Integer);
        const Token raw_array_len = P_next_token(p);
        if (Token_cmp(raw_array_len, "auto")) {
            UNIMPLEMENTED("Array<T, auto> auto-sized array not implemented yet");
        }
        const int64_t array_len = EX_token_to_int(p, raw_array_len);

        if (array_len < 0) {
            P_syntax_error(p, raw_array_len, SV_from_str("negative sized arrays are forbidden"));
            exit(1);
        }
        if (array_len == 0) {
            P_syntax_error(p, raw_array_len, SV_from_str("zero sized arrays are forbidden"));
            exit(1);
        }

        const Token end = P_require_next_be(p, TT_Greater, SV_from_str_len(">", 1));

        const Identifier t_name = P_to_identifier(p, name);

        return (IR_typeuse){
            .info.named = {t_name, (TokenSpan){*name}},
            .tt_ty = IR_TY_Array,
            .contained = irt,
            .array_const_size = array_len,
            // .full_typeuse_span = TS_merge(&(TokenSpan){*name}, &(TokenSpan){*end})
        };
    }

    if (Token_cmp(name, "List")) {
        return P_get_typeuse_intern_single_generic(p, name, IR_TY_List, false);
    }

    if (Token_cmp(name, "Box")) {
        return P_get_typeuse_intern_single_generic(p, name, IR_TY_Box, false);
    }

    if (Token_cmp(name, "Slice")) {
        return P_get_typeuse_intern_single_generic(p, name, IR_TY_Slice, false);
    }

    if (P_has_next(p) && Token_cmp(P_peek_token(p), "<")) {
        P_next_token(p);

        IR_typeuse_LIST irts = IR_typeuse_LIST_new();

        // SV end;
        while (1) {
            IR_typeuse_LIST_push(&irts, P_get_typeuse(p, allow_void));
            P_require_having_next_token(p, TT_Comma);
            const Token t = P_next_token(p);
            if (Token_cmp(t, ">")) {
                // end = *t;
                break;
            }
            if (!Token_cmp(t, ",")) {
                P_syntax_error(p, t, SV_from_str("expected either ',' or '>'"));
                exit(1);
            }
        }

        IR_typeuse_LIST_shrink(&irts);

        return (IR_typeuse){
            .tt_ty = IR_TY_Generic,
            .info.named = {
                id_name,
                (TokenSpan){*name}
            },
            .contained = irts.array, // res,
            .contained_len = irts.len,
            // .full_typeuse_span = TS_merge(&(TokenSpan){*name}, &(TokenSpan){end})
        };
    }
    return (IR_typeuse){
        .info.named = {
            id_name,
            (TokenSpan){*name}
        },
        .tt_ty = IR_TY_Named,
        // .full_typeuse_span = {*name}
    };
}

// -------------------------------------------

/// returns current block's index
size_t P_get_current_block_index(const Parser* p) {
    return p->func_scope_blocks->len - 1;
}

bool P_emit_block_is_empty(const Parser* p) {
    return p->func_scope_blocks->array[p->func_scope_blocks->len - 1].instructions.len == 0;
}

void P_emit_to_block(const Parser* p, const size_t block_nth, const IR_instr instr,
                     const IR_instr_error_info error_info) {
    if (block_nth >= p->func_scope_blocks->len) {
        INTERNAL_ERROR("invalid block position (not that many blocks)");
    }
    IR_block* block = &p->func_scope_blocks->array[block_nth];

    IR_instr new_instr = instr;
    IR_instr_error_info_LIST_push(p->func_error_info_list, error_info);
    new_instr.error_info_index = p->func_error_info_list->len - 1;

    IR_instr_LIST_push(&block->instructions, new_instr);
}

void P_emit_to_block_start(const Parser* p, const size_t block_nth, const IR_instr instr,
                           const IR_instr_error_info error_info) {
    if (block_nth >= p->func_scope_blocks->len) {
        INTERNAL_ERROR("invalid block position (not that many blocks)");
    }
    IR_block* block = &p->func_scope_blocks->array[block_nth];
    IR_instr_LIST_push(&block->instructions, instr); // used to make space

    memmove(&block->instructions.array[1], &block->instructions.array[0],
            (block->instructions.len - 1) * sizeof(IR_instr));

    IR_instr new_instr = instr;
    IR_instr_error_info_LIST_push(p->func_error_info_list, error_info);
    new_instr.error_info_index = p->func_error_info_list->len - 1;

    block->instructions.array[0] = new_instr;
}

IR_SSA_binding_LIST IR_SSA_bindings_deep_clone(const IR_SSA_binding_LIST* original) {
    const IR_SSA_binding_LIST new = (IR_SSA_binding_LIST){
        .len = original->len,
        .cap = original->cap,
        .array = malloc(sizeof(IR_SSA_binding) * original->cap)
    };
    memcpy(new.array, original->array, sizeof(IR_SSA_binding) * original->len);
    return new;
}

/// provide new independent copy of bindings on copy
void P_append_block_with_bindings(const Parser* p, const IR_SSA_binding_LIST bindings) {
    IR_block_LIST_push(p->func_scope_blocks, (IR_block){
                           .instructions = IR_instr_LIST_new(),
                           .bindings = bindings,
                       });
}

void P_append_block(const Parser* p) {
    P_append_block_with_bindings(p, IR_SSA_binding_LIST_new());
}

void P_emit(const Parser* p, const IR_instr instr, const IR_instr_error_info error_info) {
    if (p->func_scope_blocks->len == 0) {
        P_append_block(p);
    }
    IR_block* last = &p->func_scope_blocks->array[p->func_scope_blocks->len - 1];
    IR_instr new_instr = instr;

    IR_instr_error_info_LIST_push(p->func_error_info_list, error_info);
    new_instr.error_info_index = p->func_error_info_list->len - 1;

    IR_instr_LIST_push(&last->instructions, new_instr);
}

IR_SSA_binding_LIST* P_get_current_block_bindings(const Parser* p) {
    if (p->func_scope_blocks->len == 0) {
        return NULL;
    }
    return &p->func_scope_blocks->array[p->func_scope_blocks->len - 1].bindings;
}


IR_temporal P_new_temporal(Parser* p) {
    return (IR_temporal){.val = ++p->temporal_fn_cnt};
}



void P_get_var_dec_stmt(Parser* p) {
    // IR_typeuse* ty = malloc(sizeof(IR_typeuse));
    const SV* ty_sv = P_peek_token(p);
    IR_typeuse ty = P_get_typeuse(p, false);

    P_require_having_next_token(p, TT_Identifier);
    // const size_t name_token_pos = p->positioner.pos;
    const Token raw_name = P_next_token(p);
    const TokenSpan name_span = (TokenSpan){*raw_name};
    const Identifier name = P_to_identifier(p, raw_name);

    // do a scope check
    for (size_t n = p->compilation_scope.len; n > 0; n--) {
        const P_compilation_scope* scope = &p->compilation_scope.array[n - 1];

        for (size_t k = 0; k < scope->variables.len; k++) {
            const P_variable_record* var = &scope->variables.array[k];
            if (var->name.identifier == name) {
                P_syntax_error(p, &var->name.span.sv,
                               SV_from_str("declared here"));
                P_syntax_error(p, raw_name, SV_from_str("redeclaration"));
                exit(1);
            }
        }
    }

    // insert into scope
    P_variable_record_LIST_push(&P_compilation_scope_LIST_peek_last(&p->compilation_scope)->variables,
                                (P_variable_record){
                                    .name = {name, (TokenSpan){*raw_name}},
                                    .type = ty
                                });

    P_require_next_be(p, TT_Equal, SV_from_str_len("=", 1));

    // P_emit(p, (IR_instr){
    //     .ty = IR_INST_TY__VAR_DECL,
    //     .p1.name = name,
    //     .p2.type = ty,
    // });

    // const size_t var_decl_block = p->func_scope_blocks->len - 1;
    // const IR_block* last = &p->func_scope_blocks->array[var_decl_block];
    // const size_t var_decl_nth = last->instructions.len - 1;

    const IR_temporal_with_span expr_rs = P_get_expr(p);
    const IR_temporal move_dest = P_new_temporal(p);
    const IR_temporal_with_span rs = (IR_temporal_with_span){move_dest, expr_rs.span};

    P_emit(p, (IR_instr){
               .ty = IR_INST_TY__MOVE,
               .info.move = {
                   .dest = move_dest,
                   .src = expr_rs.temp
               }
           }, (IR_instr_error_info){.kind = IR_IEI_move, .info.move = {.value = rs.span}});


    IR_typeuse* ttype = malloc(sizeof(IR_typeuse));
    *ttype = ty;

    const TokenSpan whole = TS_merge(&(TokenSpan){*ty_sv}, &rs.span);
    P_emit(p, (IR_instr){
               .ty = IR_INST_TY__VAR_DECL,
               .info.var_decl = {
                   .var_name = {name, (TokenSpan){*raw_name}},
                   .dest = rs.temp,
                   .type = ttype
               }
           }, (IR_instr_error_info){
               .kind = IR_IEI_variable_declaration,
               .info.variable_declaration = {
                   .name = name_span,
                   // .type = ty.full_typeuse_span,
                   .expr = rs.span,
                   .whole = whole
               }
           });

    // // patch VAR_DECL.dest with RS
    // p->func_scope_blocks->array[var_decl_block].instructions.array[var_decl_nth].dest.val = rs.val;


    // var declr

    IR_SSA_binding_LIST* binds = P_get_current_block_bindings(p);
    bool bind_find = false;
    for (size_t n = 0; n < binds->len; n++) {
        if (binds->array[n].variable.identifier == name) {
            bind_find = true;
            binds->array[n].bind = rs.temp;
        }
    }
    if (!bind_find) {
        IR_SSA_binding_LIST_push(binds, (IR_SSA_binding){
                                     .variable = {name, (TokenSpan){*raw_name}},
                                     .bind = rs.temp
                                 });
    }

    P_require_semicolon();
}

bool P_block_is_terminated(const Parser* p, const size_t block_nth) {
    const IR_block* block = &p->func_scope_blocks->array[block_nth];
    if (block->instructions.len == 0) return false;
    const IR_instr_TY last = block->instructions.array[block->instructions.len - 1].ty;
    return last == IR_INST_TY__RETURN
           || last == IR_INST_TY__RETURN_VOID
           || last == IR_INST_TY__BRANCH
           || last == IR_INST_TY__JUMP;
}

IR_SSA_binding_LIST P_PHI_CONDITIONAL_BRANCHING_MERGER(Parser* restrict p, const IR_SSA_binding_LIST* restrict a,
                                                       const size_t a_block_id, const IR_SSA_binding_LIST* restrict b,
                                                       const size_t b_block_id, const TokenSpan trigger) {
    IR_SSA_binding_LIST out = IR_SSA_binding_LIST_new();
    for (size_t n_a = 0; n_a < a->len; n_a++) {
        const IR_SSA_binding a_val = a->array[n_a];
        for (size_t n_b = 0; n_b < b->len; n_b++) {
            const IR_SSA_binding b_val = b->array[n_b];
            if (a_val.variable.identifier == b_val.variable.identifier) {
                if (a_val.bind.val == b_val.bind.val) {
                    IR_SSA_binding_LIST_push(&out, a_val);
                } else {
                    const IR_temporal phi = P_new_temporal(p);

                    IR_temporal_block_phi_record_LIST* args = malloc(sizeof(IR_temporal_block_phi_record_LIST));
                    *args = IR_temporal_block_phi_record_LIST_new();
                    IR_temporal_block_phi_record_LIST_push(args, (IR_temporal_block_phi_record){
                                                               .block = a_block_id,
                                                               .temp = a_val.bind
                                                           });
                    IR_temporal_block_phi_record_LIST_push(args, (IR_temporal_block_phi_record){
                                                               .block = b_block_id,
                                                               .temp = b_val.bind
                                                           });

                    P_emit(p, (IR_instr){
                               .ty = IR_INST_TY__PHI,
                               .info.phi = {
                                   .dest = phi,
                                   .args = args
                               }
                           }, (IR_instr_error_info){
                               .kind = IR_IEI_phi,
                               .info.phi = {trigger}
                           });

                    IR_SSA_binding_LIST_push(&out, (IR_SSA_binding){
                                                 .variable = a_val.variable,
                                                 .bind = phi
                                             });
                }
            }
        }
    }

    return out;
}

void P_get_if_stmt(Parser* p) {
    const Token if_kw = P_next_token(p);
    if (!Token_cmp(if_kw, "if")) {
        INTERNAL_ERROR("should be 'if' (from calling this fn)")
    }


    const IR_temporal_with_span cmp_rs = P_get_expr(p);

    const size_t block_with_cmp = P_get_current_block_index(p);
    const IR_SSA_binding_LIST cmp_bindings = *P_get_current_block_bindings(p);

    P_append_block_with_bindings(p, IR_SSA_bindings_deep_clone(&cmp_bindings));
    const size_t if_section_start_block = P_get_current_block_index(p);
    P_get_code_section(p);
    const size_t if_section_end_block = P_get_current_block_index(p);
    const IR_SSA_binding_LIST if_bindings = *P_get_current_block_bindings(p);

    size_t else_section_start_block;
    size_t else_section_end_block;

    size_t after_block;

    if (P_has_next(p)) {
        const Token nx = P_peek_token(p);
        if (!Token_cmp(nx, "else")) {
            P_append_block(p);

            else_section_start_block = P_get_current_block_index(p);
            else_section_end_block = P_get_current_block_index(p);
            after_block = P_get_current_block_index(p);


            IR_SSA_binding_LIST* cur = P_get_current_block_bindings(p);
            IR_SSA_binding_LIST_free(cur);
            if (!P_block_is_terminated(p, if_section_end_block)) {
                *cur = P_PHI_CONDITIONAL_BRANCHING_MERGER(
                    p, &cmp_bindings, block_with_cmp, &if_bindings, if_section_end_block,
                    (TokenSpan){*if_kw}
                );
            } else {
                // only block_with_cmp reaches merge via false-branch; no PHI
                *cur = IR_SSA_bindings_deep_clone(&cmp_bindings);
            }

            goto end;
        }

        P_next_token(p); // else
        P_require_having_next_token(p, TT_StartCurlyBracket);
        const Token nx2 = P_peek_token(p);
        if (Token_cmp(nx2, "{")) {
            P_append_block_with_bindings(p, IR_SSA_bindings_deep_clone(&cmp_bindings));
            else_section_start_block = P_get_current_block_index(p);
            P_get_code_section(p);
            else_section_end_block = P_get_current_block_index(p);

            const IR_SSA_binding_LIST else_bindings = *P_get_current_block_bindings(p);

            P_append_block(p);

            after_block = P_get_current_block_index(p);

            const bool if_term = P_block_is_terminated(p, if_section_end_block);
            const bool els_term = P_block_is_terminated(p, else_section_end_block);


            IR_SSA_binding_LIST* cur = P_get_current_block_bindings(p);
            IR_SSA_binding_LIST_free(cur);

            if (!if_term && !els_term) {
                *cur = P_PHI_CONDITIONAL_BRANCHING_MERGER(
                    p, &if_bindings, if_section_end_block, &else_bindings, else_section_end_block,
                    (TokenSpan){*nx}
                );
            } else if (!if_term) {
                *cur = IR_SSA_bindings_deep_clone(&if_bindings);
            } else if (!els_term) {
                *cur = IR_SSA_bindings_deep_clone(&else_bindings);
            }
            // both terminated: merge block is unreachable, leave bindings empty
        } else if (Token_cmp(nx2, "if")) {
            P_append_block_with_bindings(p, IR_SSA_bindings_deep_clone(&cmp_bindings));
            else_section_start_block = P_get_current_block_index(p);

            P_get_if_stmt(p);

            else_section_end_block = P_get_current_block_index(p);
            const IR_SSA_binding_LIST else_bindings = *P_get_current_block_bindings(p);

            const bool if_term = P_block_is_terminated(p, if_section_end_block);
            const bool els_term = P_block_is_terminated(p, else_section_end_block);

            P_append_block(p);

            after_block = P_get_current_block_index(p);

            IR_SSA_binding_LIST* cur = P_get_current_block_bindings(p);
            IR_SSA_binding_LIST_free(cur);

            if (!if_term && !els_term) {
                *cur = P_PHI_CONDITIONAL_BRANCHING_MERGER(
                    p, &if_bindings, if_section_end_block, &else_bindings, else_section_end_block,
                    (TokenSpan){*nx2}
                );
            } else if (!if_term) {
                *cur = IR_SSA_bindings_deep_clone(&if_bindings);
            } else if (!els_term) {
                *cur = IR_SSA_bindings_deep_clone(&else_bindings);
            }

            // both terminated: merge block unreachable
        } else {
            P_syntax_error(p, nx2, SV_from_str("expected 'if' or '{'"));
            exit(1);
        }
    } else {
        expected_token_error(&p->source, &p->source_filename, &p->positioner.ref->array[p->positioner.ref->len - 1],
                             "function end");
        exit(1);
    }

end:


    P_emit_to_block(p, block_with_cmp, (IR_instr){
                        .ty = IR_INST_TY__BRANCH,
                        .info.branch = {
                            .cond = cmp_rs.temp,
                            .branch_true = if_section_start_block,
                            .branch_false = else_section_start_block
                        }
                    }, (IR_instr_error_info){
                        .kind = IR_IEI_branch,
                        .info.branch = {
                            .cmp_expr = cmp_rs.span
                        }
                    });

    if (!P_block_is_terminated(p, if_section_end_block)) {
        P_emit_to_block(p, if_section_end_block, (IR_instr){
                            .ty = IR_INST_TY__JUMP,
                            .info.jump = {after_block}
                        }, (IR_instr_error_info){.kind = IR_IEI_none});
    }

    if (else_section_end_block != after_block) {
        if (!P_block_is_terminated(p, else_section_end_block)) {
            P_emit_to_block(p, else_section_end_block, (IR_instr){
                                .ty = IR_INST_TY__JUMP,
                                .info.jump = {after_block}
                            }, (IR_instr_error_info){.kind = IR_IEI_none});
        }
    }
}

void P_get_while_stmt(Parser* p) {
    const Token while_kw = P_next_token(p);
    const TokenSpan while_kw_span = (TokenSpan){*while_kw};
    if (!Token_cmp(while_kw, "while")) {
        INTERNAL_ERROR("should be 'while' (from calling this fn)")
    }

    const IR_SSA_binding_LIST pre_header_bindings = *P_get_current_block_bindings(p);
    const size_t pre_header_block = P_get_current_block_index(p);

    P_append_block(p); // header

    const size_t header_block_start = P_get_current_block_index(p);

    P_emit_to_block(p, pre_header_block, (IR_instr){
                        .ty = IR_INST_TY__JUMP,
                        .info.jump = {header_block_start}
                    }, (IR_instr_error_info){.kind = IR_IEI_none});

    {
        IR_SSA_binding_LIST* binds = P_get_current_block_bindings(p);
        for (size_t n = 0; n < pre_header_bindings.len; n++) {
            const IR_temporal temp = P_new_temporal(p);
            IR_SSA_binding_LIST_push(binds, (IR_SSA_binding){
                                         .bind = temp,
                                         .variable = pre_header_bindings.array[n].variable
                                     });
        }
    }
    const IR_SSA_binding_LIST header_temp_bindings = *P_get_current_block_bindings(p);


    const IR_temporal_with_span condition = P_get_expr(p);
    const size_t header_block_end = P_get_current_block_index(p);

    P_append_block_with_bindings(p, IR_SSA_bindings_deep_clone(&header_temp_bindings)); // body start
    const size_t body_blocks_start = P_get_current_block_index(p);
    P_get_code_section(p);
    const size_t body_blocks_end = P_get_current_block_index(p);

    P_emit(p, (IR_instr){
               .ty = IR_INST_TY__JUMP,
               .info.jump = {header_block_start}
           }, (IR_instr_error_info){.kind = IR_IEI_none});

    const IR_SSA_binding_LIST block_end_bindings = *P_get_current_block_bindings(p);


    // now resolve phis
    for (size_t n = 0; n < block_end_bindings.len; n++) {
        for (size_t k = 0; k < header_temp_bindings.len; k++) {
            if (block_end_bindings.array[n].variable.identifier == header_temp_bindings.array[k].variable.identifier) {
                IR_temporal intake_temporal = {.val = 0};
                bool intake_temporal_found = false;
                if (pre_header_bindings.array[n].variable.identifier == header_temp_bindings.array[k].variable.
                    identifier) {
                    intake_temporal = pre_header_bindings.array[n].bind;
                    intake_temporal_found = true;
                }
                if (!intake_temporal_found) {
                    INTERNAL_ERROR("binding disappeared 1n | while SSA phi post calc")
                }

                if (block_end_bindings.array[n].bind.val == header_temp_bindings.array[k].bind.val) {
                    // should patch all usage of 'header_temp_bindings.array[k].bind.val' with 'intake_temporal'
                    // header_block_start - body_blocks_end

                    const IR_temporal what_to_replace = header_temp_bindings.array[k].bind;
                    const IR_temporal replace_with = intake_temporal;

                    for (size_t block_n = header_block_start; block_n <= body_blocks_end; block_n++) {
                        const IR_block* block = &p->func_scope_blocks->array[block_n];
                        for (size_t bn = 0; bn < block->bindings.len; bn++) {
                            if (block->bindings.array[bn].bind.val == what_to_replace.val) {
                                block->bindings.array[bn].bind.val = replace_with.val;
                                break;
                            }
                        }

                        for (size_t b = 0; b < block->instructions.len; b++) {
                            IR_instr* instr = &block->instructions.array[b];
                            switch (instr->ty) {
                                case IR_INST_TY__INVALID:
                                    break;
                                case IR_INST_TY__PHI: {
                                    for (size_t sn = 0; sn < instr->info.phi.args->len; sn++) {
                                        const IR_temporal_block_phi_record val = instr->info.phi.args->array[sn];
                                        if (val.temp.val == what_to_replace.val) {
                                            instr->info.phi.args->array[sn].temp.val = replace_with.val;
                                        }
                                    }
                                    break;
                                }
                                case IR_INST_TY__DROP: {
                                    if (instr->info.drop.dest.val == what_to_replace.val) {
                                        instr->info.drop.dest.val = replace_with.val;
                                    }
                                    break;
                                }
                                case IR_INST_TY__MOVE: {
                                    if (instr->info.move.src.val == what_to_replace.val) {
                                        instr->info.move.src.val = replace_with.val;
                                    }
                                    break;
                                }
                                case IR_INST_TY__ASSIGN: {
                                    if (instr->info.assign.new.val == what_to_replace.val) {
                                        instr->info.assign.new.val = replace_with.val;
                                    }
                                    if (instr->info.assign.old.val == what_to_replace.val) {
                                        instr->info.assign.old.val = replace_with.val;
                                    }
                                    break;
                                }
                                case IR_INST_TY__REF_OF: {
                                    if (instr->info.ref_of.src.val == what_to_replace.val) {
                                        instr->info.ref_of.src.val = replace_with.val;
                                    }
                                    break;
                                }
                                case IR_INST_TY__EXPLICIT_CAST: {
                                    if (instr->info.explicit_cast.src.val == what_to_replace.val) {
                                        instr->info.explicit_cast.src.val = replace_with.val;
                                    }
                                    break;
                                }
                                case IR_INST_TY__VAR_DECL:
                                    // SHOULD NOT HAVE ANYTHING TO REPLACE
                                    break;
                                case IR_INST_TY__STORE_PTR: {
                                    if (instr->info.store_ptr.value.val == what_to_replace.val) {
                                        instr->info.store_ptr.value.val = replace_with.val;
                                    }
                                    break;
                                }
                                case IR_INST_TY__CONST_INT:
                                case IR_INST_TY__CONST_FLOAT:
                                case IR_INST_TY__CONST_STRING:
                                    break;
                                case IR_INST_TY__MAKE_ARRAY: {
                                    for (size_t sn = 0; sn < instr->info.make_array.items->len; sn++) {
                                        if (instr->info.make_array.items->array[sn].val == what_to_replace.val) {
                                            instr->info.make_array.items->array[sn].val = replace_with.val;
                                        }
                                    }
                                    break;
                                }
                                case IR_INST_TY__MAKE_STRUCT: {
                                    for (size_t sn = 0; sn < instr->info.make_struct.args->len; sn++) {
                                        if (instr->info.make_struct.args->array[sn].value.val == what_to_replace.val) {
                                            instr->info.make_struct.args->array[sn].value.val = replace_with.val;
                                        }
                                    }
                                    break;
                                }
                                case IR_INST_TY__MAKE_ENUM: {
                                    for (size_t sn = 0; sn < instr->info.make_enum.args->len; sn++) {
                                        if (instr->info.make_enum.args->array[sn].val == what_to_replace.val) {
                                            instr->info.make_enum.args->array[sn].val = replace_with.val;
                                        }
                                    }
                                    break;
                                }
                                case IR_INST_TY__OP_ADD:
                                case IR_INST_TY__OP_SUB:
                                case IR_INST_TY__OP_DIV:
                                case IR_INST_TY__OP_MUL:
                                case IR_INST_TY__OP_POW:
                                case IR_INST_TY__OP_OR:
                                case IR_INST_TY__OP_AND:
                                case IR_INST_TY__OP_CMP_EQ:
                                case IR_INST_TY__OP_CMP_NOT_EQ:
                                case IR_INST_TY__OP_CMP_LESS:
                                case IR_INST_TY__OP_CMP_LESS_EQ:
                                case IR_INST_TY__OP_CMP_GREATER:
                                case IR_INST_TY__OP_CMP_GREATER_EQ: {
                                    if (instr->info.math_cmp_boolean_op.src1.val == what_to_replace.val) {
                                        instr->info.math_cmp_boolean_op.src1.val = replace_with.val;
                                    }
                                    if (instr->info.math_cmp_boolean_op.src2.val == what_to_replace.val) {
                                        instr->info.math_cmp_boolean_op.src2.val = replace_with.val;
                                    }
                                    break;
                                }
                                case IR_INST_TY__BRANCH:
                                    if (instr->info.branch.cond.val == what_to_replace.val) {
                                        instr->info.branch.cond.val = replace_with.val;
                                    }
                                    break;
                                case IR_INST_TY__JUMP:
                                case IR_INST_TY__RETURN_VOID:
                                    break;
                                case IR_INST_TY__RETURN:
                                    if (instr->info.return_value.val == what_to_replace.val) {
                                        instr->info.return_value.val = replace_with.val;
                                    }
                                    break;
                                case IR_INST_TY__GET_STRUCT_FIELD_PTR: {
                                    if (instr->info.get_struct_field_ptr.struct_ptr.val == what_to_replace.val) {
                                        instr->info.get_struct_field_ptr.struct_ptr.val = replace_with.val;
                                    }
                                    break;
                                }
                                case IR_INST_TY__INDEX_PTR: {
                                    if (instr->info.index_ptr.array_ptr.val == what_to_replace.val) {
                                        instr->info.index_ptr.array_ptr.val = replace_with.val;
                                    }
                                    if (instr->info.index_ptr.index_val.val == what_to_replace.val) {
                                        instr->info.index_ptr.index_val.val = replace_with.val;
                                    }
                                    break;
                                }
                                case IR_INST_TY__READ_PTR: {
                                    if (instr->info.read_ptr.ptr.val == what_to_replace.val) {
                                        instr->info.read_ptr.ptr.val = replace_with.val;
                                    }
                                    break;
                                }
                                case IR_INST_TY__FN_CALL_RETVOID: {
                                    for (size_t sn = 0; sn < instr->info.fn_call_retvoid.args->len; sn++) {
                                        if (instr->info.fn_call_retvoid.args->array[sn].val == what_to_replace.val) {
                                            instr->info.fn_call_retvoid.args->array[sn].val = replace_with.val;
                                        }
                                    }
                                    break;
                                }
                                case IR_INST_TY__FN_CALL_RETVAL: {
                                    for (size_t sn = 0; sn < instr->info.fn_call_retval.args->len; sn++) {
                                        if (instr->info.fn_call_retval.args->array[sn].val == what_to_replace.val) {
                                            instr->info.fn_call_retval.args->array[sn].val = replace_with.val;
                                        }
                                    }
                                    break;
                                }
                                case IR_INST_TY__MATCH_META__TARGET: {
                                    if (instr->info.match_meta__target.target.val == what_to_replace.val) {
                                        instr->info.match_meta__target.target.val = replace_with.val;
                                    }
                                    break;
                                }
                                case IR_INST_TY__MATCH_META__MATCH: {
                                    if (instr->info.match_meta__match.target.val == what_to_replace.val) {
                                        instr->info.match_meta__match.target.val = replace_with.val;
                                    }
                                    break;
                                }
                                case ENUM_GET_FIELD_PTR: {
                                    if (instr->info.enum_get_field_ptr.enum_src.val == what_to_replace.val) {
                                        instr->info.enum_get_field_ptr.enum_src.val = replace_with.val;
                                    }
                                    break;
                                }
                                case IR_INST_TY__ENUM_VARIANT_NUMBER_EXTRACT: {
                                    if (instr->info.enum_variant_number_extract.enum_src.val == what_to_replace.val) {
                                        instr->info.enum_variant_number_extract.enum_src.val = replace_with.val;
                                    }
                                    break;
                                }
                                case IR_INST_TY__ENUM_VARIANT_NUMBER_FROM_TYPE:
                                    break;
                            }
                        }
                    }
                } else {
                    // emit phi as normal
                    IR_temporal_block_phi_record_LIST* args = malloc(sizeof(IR_temporal_block_phi_record_LIST));
                    *args = IR_temporal_block_phi_record_LIST_new();

                    IR_temporal_block_phi_record_LIST_push(args, (IR_temporal_block_phi_record){
                                                               .block = pre_header_block,
                                                               .temp = intake_temporal
                                                           });

                    IR_temporal_block_phi_record_LIST_push(args, (IR_temporal_block_phi_record){
                                                               .block = body_blocks_end,
                                                               .temp = block_end_bindings.array[n].bind
                                                           });

                    // block_end + pre_header merge
                    P_emit_to_block_start(p, header_block_start, (IR_instr){
                                              .ty = IR_INST_TY__PHI,
                                              .info.phi = {
                                                  .dest = header_temp_bindings.array[k].bind,
                                                  .args = args
                                              }
                                          }, (IR_instr_error_info){
                                              .kind = IR_IEI_phi,
                                              .info.phi = {while_kw_span}
                                          });
                }

                break;
            }
        }
    }


    P_append_block_with_bindings(p, IR_SSA_bindings_deep_clone(&header_temp_bindings)); // exit
    const size_t exit_block_start = P_get_current_block_index(p);


    P_emit_to_block(p, header_block_end, (IR_instr){
                        .ty = IR_INST_TY__BRANCH,
                        .info.branch = {
                            .cond = condition.temp,
                            .branch_true = body_blocks_start,
                            .branch_false = exit_block_start
                        }
                    }, (IR_instr_error_info){
                        .kind = IR_IEI_branch,
                        .info.branch = {
                            .cmp_expr = condition.span
                        }
                    });
}


void P_get_return_stmt(Parser* p) {
    const Token ret_kw = P_next_token(p);
    if (!Token_cmp(ret_kw, "return")) {
        INTERNAL_ERROR("should be 'return' (from calling this fn)")
    }

    P_require_having_next_token(p, TT_Semicolon);
    const Token nx = P_peek_token(p);
    if (Token_cmp(nx, ";")) {
        // get all scopes, and drop 1by1
        {
            const IR_SSA_binding_LIST* bindings = P_get_current_block_bindings(p);
            for (size_t s = p->compilation_scope.len; s-- > 0;) {
                const P_compilation_scope* sc = &p->compilation_scope.array[s];
                for (size_t v = sc->variables.len; v-- > 0;) {
                    for (size_t b = 0; b < bindings->len; b++) {
                        if (
                            bindings->array[b].variable.identifier
                            !=
                            sc->variables.array[v].name.identifier
                        )
                            continue;

                        IR_typeuse* ttype = malloc(sizeof(IR_typeuse));
                        *ttype = sc->variables.array[v].type;

                        P_emit(p, (IR_instr){
                                   .ty = IR_INST_TY__DROP,
                                   .info.drop = {
                                       .var_name = sc->variables.array[v].name,
                                       .dest = bindings->array[b].bind,
                                       .type = ttype
                                   }
                               }, (IR_instr_error_info){.kind = IR_IEI_none});
                        break;
                    }
                }
            }

            // // drop args
            // // p->fn_args
            // // const IR_SSA_binding_LIST* bindings = P_get_current_block_bindings(p);
            // for (size_t n = p->fn_args->len; n-- > 0;) {
            //     const IR_typename_pair a = p->fn_args->array[n];
            //     for (size_t b = 0; b < bindings->len; b++) {
            //         if (bindings->array[b].variable.identifier != a.name.identifier) continue;
            //
            //         IR_typeuse* ttype = malloc(sizeof(IR_typeuse));
            //         *ttype = a.type;
            //
            //         P_emit(p, (IR_instr){
            //                    .ty = IR_INST_TY__DROP,
            //                    .info.drop = {
            //                        .var_name = a.name,
            //                        .dest = bindings->array[b].bind,
            //                        .type = ttype
            //                    }
            //                }, (IR_instr_error_info){.kind = IR_IEI_none});
            //         break;
            //     }
            // }
        }

        P_next_token(p);
        P_emit(p, (IR_instr){
                   .ty = IR_INST_TY__RETURN_VOID,
               }, (IR_instr_error_info){.kind = IR_IEI_none});
        return;
    }

    const IR_temporal_with_span ret = P_get_expr(p);

    const IR_temporal move_dest = P_new_temporal(p);
    P_emit(p, (IR_instr){
               .ty = IR_INST_TY__MOVE,
               .info.move = {
                   .dest = move_dest,
                   .src = ret.temp
               }
           }, (IR_instr_error_info){.kind = IR_IEI_move, .info.move = {.value = ret.span}});
    const IR_temporal_with_span final_out = (IR_temporal_with_span){move_dest, ret.span};


    // get all scopes, and drop 1by1
    {
        const IR_SSA_binding_LIST* bindings = P_get_current_block_bindings(p);
        for (size_t s = p->compilation_scope.len; s-- > 0;) {
            const P_compilation_scope* sc = &p->compilation_scope.array[s];
            for (size_t v = sc->variables.len; v-- > 0;) {
                for (size_t b = 0; b < bindings->len; b++) {
                    if (
                        bindings->array[b].variable.identifier
                        !=
                        sc->variables.array[v].name.identifier
                    )
                        continue;

                    IR_typeuse* ttype = malloc(sizeof(IR_typeuse));
                    *ttype = sc->variables.array[v].type;

                    P_emit(p, (IR_instr){
                               .ty = IR_INST_TY__DROP,
                               .info.drop = {
                                   .var_name = sc->variables.array[v].name,
                                   .dest = bindings->array[b].bind,
                                   .type = ttype
                               }
                           }, (IR_instr_error_info){.kind = IR_IEI_none});
                    break;
                }
            }
        }

        // // drop args
        // // p->fn_args
        // // const IR_SSA_binding_LIST* bindings = P_get_current_block_bindings(p);
        // for (size_t n = p->fn_args->len; n-- > 0;) {
        //     const IR_typename_pair a = p->fn_args->array[n];
        //     for (size_t b = 0; b < bindings->len; b++) {
        //         if (bindings->array[b].variable.identifier != a.name.identifier) continue;
        //
        //         IR_typeuse* ttype = malloc(sizeof(IR_typeuse));
        //         *ttype = a.type;
        //
        //         P_emit(p, (IR_instr){
        //                    .ty = IR_INST_TY__DROP,
        //                    .info.drop = {
        //                        .dest = bindings->array[b].bind,
        //                        .type = ttype,
        //                        .var_name = a.name
        //                    }
        //                }, (IR_instr_error_info){.kind = IR_IEI_none});
        //         break;
        //     }
        // }
    }

    P_emit(p, (IR_instr){
               .ty = IR_INST_TY__RETURN,
               .info.return_value = final_out.temp
           }, (IR_instr_error_info){
               .kind = IR_IEI_return_value,
               .info.return_value = {final_out.span}
           });

    P_require_semicolon();
}

/// `a += 6;`
void P_get_assignment(Parser* p) {
    const Token origin_name = P_next_token(p);
    const Identifier name = P_to_identifier(p, origin_name);
    const TokenSpan name_span = (TokenSpan){*origin_name};
    const SpannedIdentifier name_spanned = (SpannedIdentifier){name, name_span};

    const IR_temporal dest = P_variable_lookup(p, origin_name, name);

    // bool out_requires_read_ptr = true;
    const IR_temporal_with_span wrapped_result = P_expr_accesors_wrapper(p, (IR_temporal_with_span){dest, name_span});
    // const bool is_dest_ptr = dest.val != wrapped_result.temp.val; // && out_requires_read_ptr;

    P_require_having_next_token(p, TT_Equal);
    const Token assignment_type = P_next_token(p);
    const TokenSpan assignment_span = (TokenSpan){*assignment_type};

    const IR_temporal_with_span expr = P_get_expr(p);

    P_require_semicolon();

    IR_temporal_with_span out_val = expr;
    IR_temporal old = {0};

    if (Token_cmp(assignment_type, "+=")) {
        old = wrapped_result.temp;
        const IR_temporal rs = P_new_temporal(p);
        const TokenSpan whole = TS_merge(&name_span, &expr.span);
        P_emit(p, (IR_instr){
                   .ty = IR_INST_TY__OP_ADD,
                   .info.math_cmp_boolean_op = {
                       .dest = rs,
                       .src1 = wrapped_result.temp,
                       .src2 = expr.temp
                   }
               }, (IR_instr_error_info){
                   .kind = IR_IEI_math_operation,
                   .info.math_operation = {
                       .v1 = wrapped_result.span,
                       .v2 = expr.span,
                       .op = assignment_span,
                       .whole = whole
                   }
               });
        out_val = (IR_temporal_with_span){rs, expr.span};
    } else if (Token_cmp(assignment_type, "-=")) {
        old = wrapped_result.temp;
        const IR_temporal rs = P_new_temporal(p);
        const TokenSpan whole = TS_merge(&name_span, &expr.span);
        P_emit(p, (IR_instr){
                   .ty = IR_INST_TY__OP_SUB,
                   .info.math_cmp_boolean_op = {
                       .dest = rs,
                       .src1 = wrapped_result.temp,
                       .src2 = expr.temp
                   }
               }, (IR_instr_error_info){
                   .kind = IR_IEI_math_operation,
                   .info.math_operation = {
                       .v1 = wrapped_result.span,
                       .v2 = expr.span,
                       .op = assignment_span,
                       .whole = whole
                   }
               });
        out_val = (IR_temporal_with_span){rs, expr.span};
    } else if (Token_cmp(assignment_type, "*=")) {
        old = wrapped_result.temp;
        const IR_temporal rs = P_new_temporal(p);
        const TokenSpan whole = TS_merge(&name_span, &expr.span);
        P_emit(p, (IR_instr){
                   .ty = IR_INST_TY__OP_MUL,
                   .info.math_cmp_boolean_op = {
                       .dest = rs,
                       .src1 = wrapped_result.temp,
                       .src2 = expr.temp
                   }
               }, (IR_instr_error_info){
                   .kind = IR_IEI_math_operation,
                   .info.math_operation = {
                       .v1 = wrapped_result.span,
                       .v2 = expr.span,
                       .op = assignment_span,
                       .whole = whole
                   }
               });
        out_val = (IR_temporal_with_span){rs, expr.span};
    } else if (Token_cmp(assignment_type, "/=")) {
        old = wrapped_result.temp;
        const IR_temporal rs = P_new_temporal(p);
        const TokenSpan whole = TS_merge(&name_span, &expr.span);
        P_emit(p, (IR_instr){
                   .ty = IR_INST_TY__OP_DIV,
                   .info.math_cmp_boolean_op = {
                       .dest = rs,
                       .src1 = wrapped_result.temp,
                       .src2 = expr.temp
                   }
               }, (IR_instr_error_info){
                   .kind = IR_IEI_math_operation,
                   .info.math_operation = {
                       .v1 = wrapped_result.span,
                       .v2 = expr.span,
                       .op = assignment_span,
                       .whole = whole
                   }
               });
        out_val = (IR_temporal_with_span){rs, expr.span};
    } else if (Token_cmp(assignment_type, "=")) {
        old = dest;
    } else {
        P_syntax_error(p, assignment_type, SV_from_str("expected one of '=', '+=', '-=', '*=', or '/='"));
        exit(1);
    }

    // if (!is_dest_ptr) {
    //     IR_SSA_binding_LIST* binds = P_get_current_block_bindings(p);
    //     bool found = false;
    //     for (size_t n = 0; n < binds->len; n++) {
    //         if (binds->array[n].variable.identifier == name) {
    //             found = true;
    //             break;
    //         }
    //     }
    //     if (found) {
    //         IR_typeuse* type = malloc(sizeof(IR_typeuse));
    //         *type = P_variable_type_lookup(p, origin_name, name);
    //         P_emit(p, (IR_instr){
    //                    .ty = IR_INST_TY__DROP,
    //                    .info.drop = {
    //                        .var_name = name_spanned,
    //                        .dest = dest,
    //                        .type = type,
    //                    }
    //                }, (IR_instr_error_info){.kind = IR_IEI_none});
    //     }
    // }


    // if (is_dest_ptr) {
    //     P_emit(p, (IR_instr){
    //                .ty = IR_INST_TY__STORE_PTR,
    //                .info.store_ptr = {
    //                    .ptr = wrapped_result.temp,
    //                    .value = out_val.temp
    //                }
    //            }, (IR_instr_error_info){
    //                .kind = IR_IEI_store_ptr,
    //                .info.store_ptr = {
    //                    .to = wrapped_result.span,
    //                    .ptr = out_val.span,
    //                    .expr = expr.span,
    //                }
    //            });
    // } else
        {
        const IR_temporal assign_dest = P_new_temporal(p);

        // P_emit(p, (IR_instr){
        //            .ty = IR_INST_TY__MOVE,
        //            .info.move = {
        //                .dest = move_dest,
        //                .src = out_val.temp
        //            }
        //        }, (IR_instr_error_info){.kind = IR_IEI_move, .info.move = {.value = expr.span}});
        P_emit(p, (IR_instr) {
            .ty = IR_INST_TY__ASSIGN,
            .info.assign = {
                .dest = assign_dest,
                .old = old,
                .new = out_val.temp
            }
        }, (IR_instr_error_info){});

        const IR_temporal_with_span final_out = (IR_temporal_with_span){assign_dest, out_val.span};

        IR_SSA_binding_LIST* binds = P_get_current_block_bindings(p);
        bool found = false;
        for (size_t n = 0; n < binds->len; n++) {
            if (binds->array[n].variable.identifier == name) {
                found = true;
                binds->array[n].bind = final_out.temp;
                break;
            }
        }
        if (!found) {
            IR_SSA_binding_LIST_push(binds, (IR_SSA_binding){
                .variable = {name, (TokenSpan){*origin_name}},
                .bind = final_out.temp,
            });
        }
    }

}


void P_get_function_call_stmt(Parser* p) {
    P_require_having_next_token(p, TT_Identifier);
    const Token t = P_next_token(p);
    const Identifier name = P_to_identifier(p, t);
    const TokenSpan name_span = (TokenSpan){*t};

    P_require_having_next_token(p, TT_StartBracket);
    const Token nx = P_next_token(p);
    if (!Token_cmp(nx, "(")) {
        INTERNAL_ERROR("expected '(' based on fn_call_stmt being called");
    }

    IR_temporal_LIST* args = malloc(sizeof(IR_temporal_LIST));
    *args = IR_temporal_LIST_new();
    TokenSpan_LIST args_spans = TokenSpan_LIST_new();

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
        TokenSpan_LIST_push(&args_spans, expr.span);

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

    TokenSpan_LIST_shrink(&args_spans);
    P_emit(p, (IR_instr){
               .ty = IR_INST_TY__FN_CALL_RETVOID,
               .info.fn_call_retvoid = {
                   .name = {name, (TokenSpan){*t}},
                   .args = args
               }
           }, (IR_instr_error_info){
               .kind = IR_IEI_function_call,
               .info.function_call = {
                   .name = name_span,
                   .args = args_spans.array,
                   .args_cnt = args_spans.len,
                   .whole = TS_merge(&name_span, &end_span)
               }
           });

    P_require_semicolon();
}

Pattern* P_get_pattern(Parser* p) {
    P_require_having_next_token(p, TT_Identifier); // or literal/bracket
    Token t = P_next_token(p);

    Pattern* pat = malloc(sizeof(Pattern));

    pat->span = (TokenSpan){.sv = *t};

    if (Token_cmp(t, "_")) {
        pat->kind = PAT_WILDCARD;
        return pat;
    }

    if (t->start[0] == '\'') {
        int64_t val;
        P_parse_char_literal(p, t, &val);
        pat->kind = PAT_LITERAL_INT;
        pat->info.int_literal_val = val;
        return pat;
    }

    const bool is_negative = Token_cmp(t, "-");
    if (is_negative) {
        t = P_next_token(p); // consume the minus sign, get the number
    }

    // Check if it's a literal (integer, true/false, etc.)
    if (isdigit(t->start[0]) || Token_cmp(t, "true") || Token_cmp(t, "false")) {
        bool found_dot = false;
        for (size_t n = 1; n < t->len; n++) {
            if (t->start[n] == '.') {
                found_dot = true;
                break;
            }
        }
        if (found_dot) {
            pat->kind = PAT_LITERAL_FLOAT;
            pat->info.float_literal_val = EX_token_to_float(p, t);
        } else {
            pat->kind = PAT_LITERAL_INT;
            if (Token_cmp(t, "true")) {
                pat->info.int_literal_val = 1;
            } else if (Token_cmp(t, "false")) {
                pat->info.int_literal_val = 0;
            } else {
                int64_t v = EX_token_to_int(p, t);
                if (is_negative) {
                    v *= -1;
                }
                pat->info.int_literal_val = v;
            }
        }
        return pat;
    }

    // Check if it's an enum variant path (e.g., Option :: Some)
    if (P_has_next(p) && Token_cmp(P_peek_token(p), "::")) {
        P_next_token(p); // consume "::"
        const Token var_token = P_next_token(p);

        pat->kind = PAT_VARIANT;
        pat->info.variant = (Pattern_Variant){
            .enum_name = (SpannedIdentifier){P_to_identifier(p, t), (TokenSpan){*t}},
            .variant_name = (SpannedIdentifier){P_to_identifier(p, var_token), (TokenSpan){*var_token}},
            .sub_patterns = Pattern_LIST_new(),
        };

        // Check for payload like Some(x) or Some(10)
        if (P_has_next(p) && Token_cmp(P_peek_token(p), "(")) {
            P_next_token(p); // consume '('

            while (1) {
                if (!P_has_next(p)) break;
                if (Token_cmp(P_peek_token(p), ")")) break;
                const Pattern* pt = P_get_pattern(p);
                Pattern_LIST_push(&pat->info.variant.sub_patterns, *pt);

                if (!P_has_next(p)) break;
                if (Token_cmp(P_peek_token(p), ")")) break;

                P_require_next_be(p, TT_Comma, SV_from_str_len(",", 1));
            }

            P_require_next_be(p, TT_EndBracket, SV_from_str(")"));
        }
        return pat;
    }

    // Otherwise, treat any standalone identifier as a NEW binding (e.g., `x`)
    pat->kind = PAT_BINDING;
    const Identifier bind_identf = P_to_identifier(p, t);
    {
        const IR_SSA_binding_LIST* bl = P_get_current_block_bindings(p);
        for (size_t n = 0; n < bl->len; n++) {
            if(bl->array[n].variable.identifier == bind_identf) {
                P_syntax_error(p, t, SV_from_str_len("match binding cannot shadow local variable", 42));
                for (size_t k = 0; k < bl->len; k++) {
                    printf("-> '"SV_format"'\n", SV_v_args(bl->array[k].variable.span.sv));
                }
                exit(1);
            }
        }
    }
    pat->info.binding.name = (SpannedIdentifier){.identifier = bind_identf, .span = (TokenSpan){.sv = *t}};
    pat->info.binding.temp = P_new_temporal(p);
    return pat;
}








IR_temporal P_match_partial_case_eval(Parser* restrict p, const Pattern pat, const IR_temporal in_value_temp,
                                      const TokenSpan in_value_span, const TokenSpan match_kw,
                                      P_match_branch_patch_LIST* restrict patches, const size_t case_nth) {
    const size_t block = P_get_current_block_index(p);

    switch (pat.kind) {
        case PAT_LITERAL_INT: {
            const IR_temporal lit = P_new_temporal(p);
            P_emit_to_block(p, block, (IR_instr){
                                .ty = IR_INST_TY__CONST_INT,
                                .info.const_int = {.dest = lit, .val = pat.info.int_literal_val}
                            }, (IR_instr_error_info){
                                .kind = IR_IEI_literal,
                                .info.literal = pat.span
                            });

            const IR_temporal cmp = P_new_temporal(p);
            P_emit_to_block(p, block, (IR_instr){
                                .ty = IR_INST_TY__OP_CMP_EQ,
                                .info.math_cmp_boolean_op = {
                                    .dest = cmp,
                                    .src1 = in_value_temp,
                                    .src2 = lit
                                }
                            }, (IR_instr_error_info){
                                .kind = IR_IEI_math_operation,
                                .info.math_operation = {
                                    .v1 = in_value_span,
                                    .v2 = pat.span,
                                    .op = match_kw,
                                    .whole = match_kw,
                                }
                            });

            return cmp;
        }
        case PAT_LITERAL_FLOAT: {
            const IR_temporal lit = P_new_temporal(p);
            P_emit_to_block(p, block, (IR_instr){
                                .ty = IR_INST_TY__CONST_FLOAT,
                                .info.const_float = {.dest = lit, .val = pat.info.float_literal_val}
                            }, (IR_instr_error_info){
                                .kind = IR_IEI_literal,
                                .info.literal = pat.span
                            });

            const IR_temporal cmp = P_new_temporal(p);
            P_emit_to_block(p, block, (IR_instr){
                                .ty = IR_INST_TY__OP_CMP_EQ,
                                .info.math_cmp_boolean_op = {
                                    .dest = cmp,
                                    .src1 = in_value_temp,
                                    .src2 = lit
                                }
                            }, (IR_instr_error_info){
                                .kind = IR_IEI_math_operation,
                                .info.math_operation = {
                                    .v1 = in_value_span,
                                    .v2 = pat.span,
                                    .op = match_kw,
                                    .whole = match_kw,
                                }
                            });

            return cmp;
        }
        case PAT_BINDING: {
            // P_emit_to_block(p, block, (IR_instr){
            //                     .ty = IR_INST_TY__MOVE,
            //                     .info.move = {.src = in_value_temp, .dest = pat.info.binding.temp}
            //                 }, (IR_instr_error_info){
            //                     .kind = IR_IEI_move,
            //                     .info.move = {
            //                         .value = in_value_span
            //                     }
            //                 });
            IR_SSA_binding_LIST* bindings = P_get_current_block_bindings(p);
            IR_SSA_binding_LIST_push(bindings, (IR_SSA_binding){
                                         .variable = pat.info.binding.name,
                                         .bind = in_value_temp // pat.info.binding.temp,
                                     });
            if (p->compilation_scope.len > 0) {
                P_variable_record_LIST* l = &p->compilation_scope.array[p->compilation_scope.len - 1].variables;
                P_variable_record_LIST_push(l, (P_variable_record){
                                                .name = pat.info.binding.name,
                                            });
            }
            return (IR_temporal){0};
        }
        case PAT_VARIANT: {
            const SpannedIdentifier enum_name = pat.info.variant.enum_name;
            const SpannedIdentifier variant_name = pat.info.variant.variant_name;

            const IR_temporal v1 = P_new_temporal(p);
            const IR_temporal v2 = P_new_temporal(p);

            P_emit(p, (IR_instr){
                                .ty = IR_INST_TY__ENUM_VARIANT_NUMBER_EXTRACT,
                                .info.enum_variant_number_extract = {
                                    .dest = v1,
                                    .enum_src = in_value_temp
                                }
                            }, (IR_instr_error_info){.kind = IR_IEI_none});

            P_emit(p, (IR_instr){
                                .ty = IR_INST_TY__ENUM_VARIANT_NUMBER_FROM_TYPE,
                                .info.enum_variant_number_from_type = {
                                    .dest = v2,
                                    .enum_name = enum_name,
                                    .variant_name = variant_name
                                }
                            }, (IR_instr_error_info){.kind = IR_IEI_none});

            const IR_temporal variant_num_cmp_rs = P_new_temporal(p);
            P_emit(p, (IR_instr){
                                .ty = IR_INST_TY__OP_CMP_EQ,
                                .info.math_cmp_boolean_op = {
                                    .dest = variant_num_cmp_rs,
                                    .src1 = v1,
                                    .src2 = v2,
                                }
                            }, (IR_instr_error_info){.kind = IR_IEI_none});

            if (pat.info.variant.sub_patterns.len == 0) {
                return variant_num_cmp_rs;
            }

            P_append_block_with_bindings(p, IR_SSA_bindings_deep_clone(P_get_current_block_bindings(p)));
            const size_t vn_true_block = P_get_current_block_index(p);

            // here a branch will go
            P_match_branch_patch_LIST_push(patches, (P_match_branch_patch){
                                               .block = block,
                                               .br_rs = variant_num_cmp_rs,
                                               .case_nth = case_nth,
                                               .true_block = vn_true_block
                                           });

            IR_temporal ot = {0};
            const Pattern_LIST pl = pat.info.variant.sub_patterns;
            for (size_t n = 0; n < pl.len; n++) {
                const IR_temporal exprt_val = P_new_temporal(p);

                // P_emit_to_block(p, vn_true_block, (IR_instr){
                P_emit(p, (IR_instr){
                                    .ty = ENUM_GET_FIELD_PTR,
                                    .info.enum_get_field_ptr = {
                                        .dest = exprt_val,
                                        .enum_src = in_value_temp,
                                        .enum_name = enum_name,
                                        .variant_name = variant_name,
                                        .value_nth = n
                                    }
                                }, (IR_instr_error_info){});

                const IR_temporal nx = P_match_partial_case_eval(p, pl.array[n], exprt_val, pl.array[n].span, match_kw,
                                                                 patches, case_nth);

                if (n + 1 != pl.len && nx.val != 0) {
                    const size_t current_block = P_get_current_block_index(p);

                    P_append_block_with_bindings(p, IR_SSA_bindings_deep_clone(P_get_current_block_bindings(p)));
                    const size_t true_block = P_get_current_block_index(p);

                    // here a branch will go
                    P_match_branch_patch_LIST_push(patches, (P_match_branch_patch){
                                                       .block = current_block,
                                                       .br_rs = nx,
                                                       .case_nth = case_nth,
                                                       .true_block = true_block
                                                   });
                } else {
                    ot = nx;
                }


            }

            // // if sub-patterns were just bindings/wildcards, ot.val will be 0.
            // if (ot.val == 0) {
            //     return variant_num_cmp_rs;
            // }


            return ot;
        }
        case PAT_WILDCARD: {
            return (IR_temporal){0};
        }
    }
    UNREACHABLE()
}


void P_get_match_stmt(Parser* p) {
    const Token match_kw_token = P_next_token(p); // consume "match"
    const TokenSpan match_kw = (TokenSpan){.sv = *match_kw_token};

    const IR_temporal_with_span raw_target = P_get_expr(p);
    IR_temporal_with_span target;

    if (
        p->func_scope_blocks->len > 0 &&
        p->func_scope_blocks->array[p->func_scope_blocks->len-1].instructions.len > 0 &&
        (
            (
                p->func_scope_blocks->array[p->func_scope_blocks->len-1].instructions.array[p->func_scope_blocks->array[p->func_scope_blocks->len-1].instructions.len-1].ty == IR_INST_TY__INDEX_PTR &&
                p->func_scope_blocks->array[p->func_scope_blocks->len-1].instructions.array[p->func_scope_blocks->array[p->func_scope_blocks->len-1].instructions.len-1].info.index_ptr.dest.val == raw_target.temp.val
            ) ||
            (
                p->func_scope_blocks->array[p->func_scope_blocks->len-1].instructions.array[p->func_scope_blocks->array[p->func_scope_blocks->len-1].instructions.len-1].ty == IR_INST_TY__GET_STRUCT_FIELD_PTR &&
                p->func_scope_blocks->array[p->func_scope_blocks->len-1].instructions.array[p->func_scope_blocks->array[p->func_scope_blocks->len-1].instructions.len-1].info.get_struct_field_ptr.dest.val == raw_target.temp.val
            )
        )
    ) {
        target = raw_target;
    } else {
        target = (IR_temporal_with_span) {.temp = P_new_temporal(p), .span = raw_target.span};

        P_emit(p, (IR_instr) {
        .ty = IR_INST_TY__REF_OF,
        .info.ref_of = {
            .dest = target.temp,
            .src = raw_target.temp
        }
    }, (IR_instr_error_info) {
        .kind = IR_IEI_ref_of,
        .info.ref_of = {
           .value = raw_target.span,
           .is_from_match = true,
           .match_kw = match_kw
        },
    });
    }

    // const IR_temporal_with_span target = (IR_temporal_with_span) {.temp = P_new_temporal(p), .span = raw_target.span};





    // P_emit(p, (IR_instr) {
    //     .ty = IR_INST_TY__MOVE,
    //     .info.move = {
    //         .dest = target.temp,
    //         .src = raw_target.temp
    //     }
    // }, (IR_instr_error_info) {
    //     .kind = IR_IEI_move,
    //     .info.move = {
    //         .value = raw_target.span
    //     }
    // });


    P_emit(p, (IR_instr){
               .ty = IR_INST_TY__MATCH_META__TARGET,
               .info.match_meta__target = {.target = target.temp}
           }, (IR_instr_error_info){
               .kind = IR_IEI__MATCH_META__target,
               .info.match_meta__target = {.expr = target.span}
           });



    const size_t match_start_block = P_get_current_block_index(p);
    const IR_SSA_binding_LIST base_bindings = IR_SSA_bindings_deep_clone(P_get_current_block_bindings(p));

    P_require_next_be(p, TT_StartCurlyBracket, SV_from_str_len("{", 1));

    P_match_arm_info_LIST arms = P_match_arm_info_LIST_new();
    P_match_branch_patch_LIST patches = P_match_branch_patch_LIST_new();

    bool first = true;

    while (1) {
        P_require_having_next_token(p, TT_EndCurlyBracket);
        if (Token_cmp(P_peek_token(p), "}")) {
            P_next_token(p);
            break;
        }

        if (!first) {
            P_append_block_with_bindings(p, IR_SSA_bindings_deep_clone(&base_bindings));
        } else {
            first = false;
        }



        Pattern* pat = P_get_pattern(p); // here for some reasons current bindings contain prev binds


        const Token arrow = P_next_token(p);
        if (!Token_cmp(arrow, "=>")) {
            P_syntax_error(p, arrow, SV_from_str("expected '=>'"));
            exit(1);
        }




        const size_t pre_branch_block = P_get_current_block_index(p);

        const IR_temporal out = P_match_partial_case_eval(p, *pat, target.temp, target.span, match_kw,
                                                          &patches, arms.len);

        const size_t final_eval_block = P_get_current_block_index(p);

        P_append_block_with_bindings(p, IR_SSA_bindings_deep_clone(P_get_current_block_bindings(p)));

        const size_t start_block = P_get_current_block_index(p);

        P_get_code_section(p);

        const size_t arm_end_block = P_get_current_block_index(p);
        const bool terminated = P_block_is_terminated(p, arm_end_block);
        const IR_SSA_binding_LIST arm_bindings = IR_SSA_bindings_deep_clone(P_get_current_block_bindings(p));

        P_match_arm_info_LIST_push(&arms, (P_match_arm_info){
                                       .cond_temp = out,
                                       .is_unconditional = pat->kind == PAT_WILDCARD || pat->kind == PAT_BINDING, //out.val == 0,
                                       .pre_branch_block = pre_branch_block,
                                       .final_eval_block = final_eval_block,
                                       .start_block = start_block,
                                       .end_block = arm_end_block,
                                       .bindings = arm_bindings,
                                       .terminated = terminated,
                                       .pat = pat,
                                   });

        if (P_has_next(p) && Token_cmp(P_peek_token(p), ",")) {
            P_next_token(p);
        }
    }

    if (arms.len == 0) {
        P_syntax_error(p, match_kw_token, SV_from_str_len("match without cases", 19));
        exit(1);
    }

    P_append_block(p);
    const size_t after_block = P_get_current_block_index(p);

    IR_SSA_binding_LIST merged_bindings = IR_SSA_binding_LIST_new();


    for (size_t i = 0; i < arms.len; i++) {
        const size_t false_target = (i + 1 < arms.len) ? arms.array[i + 1].pre_branch_block : after_block;

        if (arms.array[i].is_unconditional) {
            // Wildcards/Bindings always hit their start block unconditionally
            P_emit_to_block(p, arms.array[i].pre_branch_block, (IR_instr){
                                .ty = IR_INST_TY__JUMP,
                                .info.jump = {.block = arms.array[i].start_block}
                            }, (IR_instr_error_info){.kind = IR_IEI_none});
            break; // Any code after a wildcard is unreachable anyway!
        } else if (arms.array[i].cond_temp.val == 0) {
            P_emit_to_block(p, arms.array[i].final_eval_block, (IR_instr){
                .ty = IR_INST_TY__JUMP,
                .info.jump = {.block = arms.array[i].start_block}
            }, (IR_instr_error_info){.kind = IR_IEI_none});
        } else {
            // Emit the conditional branch using our stored cond_temp
            P_emit_to_block(p, arms.array[i].final_eval_block, (IR_instr){
                                .ty = IR_INST_TY__BRANCH,
                                .info.branch = {
                                    .cond = arms.array[i].cond_temp,
                                    .branch_true = arms.array[i].start_block,
                                    .branch_false = false_target
                                }
                            }, (IR_instr_error_info){.kind = IR_IEI_branch, .info.branch = {.cmp_expr = match_kw}});
        }
    }


    for (size_t n=0; n < patches.len; n++) {
        const P_match_branch_patch pt = patches.array[n];
        const size_t false_block = (pt.case_nth + 1 == arms.len) ? after_block : arms.array[pt.case_nth+1].pre_branch_block;
        P_emit_to_block(p, pt.block, (IR_instr) {
            .ty = IR_INST_TY__BRANCH,
            .info.branch = {
                .cond = pt.br_rs,
                .branch_true = pt.true_block,
                .branch_false = false_block,
            }
        }, (IR_instr_error_info){});
    }


    size_t non_terminated_count = 0;

    for (size_t i = 0; i < arms.len; i++) {
        if (!arms.array[i].terminated) {
            non_terminated_count++;
            P_emit_to_block(p, arms.array[i].end_block, (IR_instr){
                                .ty = IR_INST_TY__JUMP,
                                .info.jump = {after_block}
                            }, (IR_instr_error_info){.kind = IR_IEI_none});
        }
    }

    // if all are terminated it means you will never return to the after block, therefore the PHI merger does not make sense
    if (non_terminated_count > 0) {
        for (size_t b = 0; b < base_bindings.len; b++) {
            const IR_SSA_binding base_bind = base_bindings.array[b];

            IR_temporal first_val = {0};
            bool all_same = true;
            bool initialized = false;

            for (size_t i = 0; i < arms.len; i++) {
                if (arms.array[i].terminated) continue;

                IR_temporal arm_val = base_bind.bind;
                for (size_t k = 0; k < arms.array[i].bindings.len; k++) {
                    if (arms.array[i].bindings.array[k].variable.identifier == base_bind.variable.identifier) {
                        arm_val = arms.array[i].bindings.array[k].bind;
                        break;
                    }
                }

                if (!initialized) {
                    first_val = arm_val;
                    initialized = true;
                } else if (arm_val.val != first_val.val) {
                    all_same = false;
                }
            }

            if (all_same || non_terminated_count == 1) {
                IR_SSA_binding_LIST_push(&merged_bindings, (IR_SSA_binding){
                                             .variable = base_bind.variable,
                                             .bind = first_val
                                         });
            } else {
                const IR_temporal phi = P_new_temporal(p);
                IR_temporal_block_phi_record_LIST* phi_args = malloc(sizeof(IR_temporal_block_phi_record_LIST));
                *phi_args = IR_temporal_block_phi_record_LIST_new();

                for (size_t i = 0; i < arms.len; i++) {
                    if (arms.array[i].terminated) continue;

                    IR_temporal arm_val = base_bind.bind;
                    for (size_t k = 0; k < arms.array[i].bindings.len; k++) {
                        if (arms.array[i].bindings.array[k].variable.identifier == base_bind.variable.identifier) {
                            arm_val = arms.array[i].bindings.array[k].bind;
                            break;
                        }
                    }

                    IR_temporal_block_phi_record_LIST_push(phi_args, (IR_temporal_block_phi_record){
                                                               .block = arms.array[i].end_block,
                                                               .temp = arm_val
                                                           });
                }

                // ADD THE FALLBACK BLOCK IF IT REACHES AFTER_BLOCK
                // The last arm's pre_branch_block (e.g., Block 4) jumps to after_block on false!
                const size_t last_arm_idx = arms.len - 1;
                const size_t fallback_block = arms.array[last_arm_idx].pre_branch_block;

                // IR_temporal_block_phi_record_LIST_push(phi_args, (IR_temporal_block_phi_record){
                //                                            .block = fallback_block,
                //                                            .temp = base_bind.bind
                //                                            // Fallback block didn't execute an arm, so it uses the base binding value!
                //                                        });

                if (!arms.array[last_arm_idx].is_unconditional) {
                    IR_temporal_block_phi_record_LIST_push(phi_args, (IR_temporal_block_phi_record){
                        .block = fallback_block,
                        .temp = base_bind.bind
                    });
                }

                P_emit(p, (IR_instr){
                           .ty = IR_INST_TY__PHI,
                           .info.phi = {
                               .dest = phi,
                               .args = phi_args
                           }
                       }, (IR_instr_error_info){
                           .kind = IR_IEI_phi,
                           .info.phi = {base_bind.variable.span}
                       });

                IR_SSA_binding_LIST_push(&merged_bindings, (IR_SSA_binding){
                                             .variable = base_bind.variable,
                                             .bind = phi
                                         });
            }
        }
    }

    IR_SSA_binding_LIST* cur_bindings = P_get_current_block_bindings(p);
    IR_SSA_binding_LIST_free(cur_bindings);
    *cur_bindings = merged_bindings;


    Pattern_LIST patterns = Pattern_LIST_new_with_capacity(arms.len);
    for (size_t n = 0; n < arms.len; n++) {
        Pattern_LIST_push(&patterns, *arms.array[n].pat);
    }
    P_emit(p, (IR_instr){
               .ty = IR_INST_TY__MATCH_META__MATCH,
               .info.match_meta__match = {
                   .target = target.temp,
                   .patterns = patterns
               }
           }, (IR_instr_error_info){
               .kind = IR_IEI__MATCH_META__match,
               .info.match_meta__match = {
                   .target_expr = target.span,
                   .match_kw = match_kw
               }
           });

    P_match_arm_info_LIST_free(&arms);
}

void P_get_stmt(Parser* p) {
    P_require_having_next_token(p, TT_Semicolon);

    const Token pt = P_peek_token(p);
    if (Token_cmp(pt, ";")) {
        return;
    }

    if (Token_cmp(pt, "if")) {
        P_get_if_stmt(p);
        return;
    }

    if (Token_cmp(pt, "while")) {
        P_get_while_stmt(p);
        return;
    }

    if (Token_cmp(pt, "return")) {
        P_get_return_stmt(p);
        return;
    }

    if (Token_cmp(pt, "match")) {
        P_get_match_stmt(p);
        return;
    }

    // do a two token lookahead
    const size_t p_pos = p->positioner.pos;

    P_next_token(p);
    const Token t2 = P_next_token(p);
    const Token t3 = P_next_token(p);
    const Token t4 = P_next_token(p);

    p->positioner.pos = p_pos;

    if (Token_cmp(t2, "<")) {
        // type
        P_get_var_dec_stmt(p);
        return;
    }

    if (Token_cmp(t2, "(")) {
        // function call
        P_get_function_call_stmt(p);
        return;
    }

    // <name>.<fn>(<args>)
    if (Token_cmp(t2, ".") && (Token_cmp(t4, "<") || Token_cmp(t4, "(")) && t3->len > 0 && isalpha(t3->start[0])) {
        P_get_expr(p);
        P_require_semicolon()
        return;
    }

    if (
        Token_cmp(t2, "+=") || Token_cmp(t2, "-=") || Token_cmp(t2, "*=") || Token_cmp(t2, "/=") ||
        Token_cmp(t2, "=") ||
        Token_cmp(t2, ".") || Token_cmp(t2, "[") // accessors
    ) {
        P_get_assignment(p);
        return;
    }

    P_get_var_dec_stmt(p);
}

void P_get_code_section(Parser* p) {
    P_require_next_be(p, TT_StartCurlyBracket, SV_from_str_len("{", 1));

    P_compilation_scope_LIST_push(&p->compilation_scope, (P_compilation_scope){
                                      .variables = P_variable_record_LIST_new(),
                                  });

    while (1) {
        P_require_having_next_token(p, TT_EndCurlyBracket);
        if (Token_cmp(P_peek_token(p), "}")) {
            break;
        }

        P_get_stmt(p);
    }

    P_compilation_scope* pop = P_compilation_scope_LIST_pop(&p->compilation_scope);

    if (
        p->func_scope_blocks->len > 0 &&
        p->func_scope_blocks->array[p->func_scope_blocks->len - 1].instructions.len > 0 &&
        (
            p->func_scope_blocks->array[p->func_scope_blocks->len - 1].instructions.array[
                p->func_scope_blocks->array[p->func_scope_blocks->len - 1].instructions.len - 1].ty ==
            IR_INST_TY__RETURN
            ||
            p->func_scope_blocks->array[p->func_scope_blocks->len - 1].instructions.array[
                p->func_scope_blocks->array[p->func_scope_blocks->len - 1].instructions.len - 1].ty ==
            IR_INST_TY__RETURN_VOID
        )) {
        // not
    } else {
        const IR_SSA_binding_LIST* bindings = P_get_current_block_bindings(p);
        for (size_t v = pop->variables.len; v-- > 0;) {
            for (size_t b = 0; b < bindings->len; b++) {
                if (bindings->array[b].variable.identifier != pop->variables.array[v].name.identifier) continue;

                IR_typeuse* ttype = malloc(sizeof(IR_typeuse));
                *ttype = pop->variables.array[v].type;

                P_emit(p, (IR_instr){
                           .ty = IR_INST_TY__DROP,
                           .info.drop = {
                               .var_name = pop->variables.array[v].name,
                               .dest = bindings->array[b].bind,
                               .type = ttype
                           }
                       }, (IR_instr_error_info){.kind = IR_IEI_none});
                break;
            }
        }
    }

    P_variable_record_LIST_free(&pop->variables);

    P_require_next_be(p, TT_EndCurlyBracket, SV_from_str_len("}", 1));
}


// -------------------------------------------

void P_get_struct_field_list(Parser* restrict p, IR_field_LIST* restrict fields) {
    while (1) {
        if (!P_has_next(p)) {
            expected_token_error(&p->source, &p->source_filename, &p->positioner.ref->array[p->positioner.ref->len - 1],
                                 "}");
            exit(1);
        }
        const Token nx = P_peek_token(p);
        if (Token_cmp(nx, "}")) {
            P_next_token(p);
            break;
        }
        const IR_typeuse ty = P_get_typeuse(p, false);
        P_require_having_next_token(p, TT_Identifier);
        const Token raw_field_name = P_next_token(p);
        const Identifier field_name = P_to_identifier(p, raw_field_name);

        IR_field_LIST_push(fields, (IR_field){
                               {field_name, (TokenSpan){*raw_field_name}},
                               ty,
                           });

        P_require_having_next_token(p, TT_EndCurlyBracket);
        const Token nx2 = P_next_token(p);
        if (Token_cmp(nx2, "}")) {
            break;
        }
        if (!Token_cmp(nx2, ";")) {
            P_syntax_error(p, nx2, SV_from_str("expected either ';' or '}'"));
            exit(1);
        }
    }
}

void P_get_top_struct(Parser* p) {
    const Token struct_kw = P_next_token(p);
    if (!Token_cmp(struct_kw, "struct")) {
        INTERNAL_ERROR("should be 'struct' (from calling this fn)")
    }

    P_require_having_next_token(p, TT_Identifier);
    const Token raw_struct_name = P_next_token(p);
    const Identifier struct_name = P_to_identifier(p, raw_struct_name);

    P_require_next_be(p, TT_StartCurlyBracket, SV_from_str_len("{", 1));

    IR_field_LIST fields = IR_field_LIST_new();


    P_get_struct_field_list(p, &fields);

    if (fields.len == 0) {
        // FORBIDDEN
        P_syntax_error(p, raw_struct_name, SV_from_str("empty structs are forbidden"));
        exit(1);
    }


    IR_structdef_LIST_push(&p->type_table.structs,
                           (IR_structdef){
                               .name = {struct_name, (TokenSpan){*raw_struct_name}},
                               .fields = fields,
                           }
    );
}


void P_get_top_fn(Parser* p) {
    p->temporal_fn_cnt = 0;
    p->compilation_scope = P_compilation_scope_LIST_new();

    const IR_typeuse fn_ret_t = P_get_typeuse(p, true);

    P_require_having_next_token(p, TT_Identifier);
    const Token raw_fn_name = P_next_token(p);
    const Identifier fn_name = P_to_identifier(p, raw_fn_name);

    P_require_next_be(p, TT_StartBracket, SV_from_str_len("(", 1));

    IR_typename_pair_LIST args = IR_typename_pair_LIST_new();

    while (1) {
        P_require_having_next_token(p, TT_EndBracket);
        if (Token_cmp(P_peek_token(p), ")")) {
            break;
        }
        const IR_typeuse ty = P_get_typeuse(p, false);

        P_require_having_next_token(p, TT_Identifier);
        const Token pre_name_token = &p->positioner.ref->array[p->positioner.pos - 1];
        const Token name = P_next_token(p);
        if (Token_cmp(name, ",")) {
            expected_token_error(&p->source, &p->source_filename, pre_name_token, "argument name");
            exit(1);
        }

        const Identifier id_name = P_to_identifier(p, name);

        IR_typename_pair_LIST_push(&args, (IR_typename_pair){.type = ty, .name = {id_name, (TokenSpan){*name}}});

        if (Token_cmp(P_peek_token(p), ")")) {
            break;
        }
        P_require_next_be(p, TT_Comma, SV_from_str_len(",", 1));
    }


    P_require_next_be(p, TT_EndBracket, SV_from_str_len(")", 1));


    // -------------------------------------------------------------

    IR_block_LIST* original_blocks = p->func_scope_blocks;
    IR_block_LIST blocks = IR_block_LIST_new();
    p->func_scope_blocks = &blocks;

    IR_instr_error_info_LIST error_info_list = IR_instr_error_info_LIST_new();
    p->func_error_info_list = &error_info_list;

    p->fn_args = &args;

    P_append_block(p);

    for (size_t n = 0; n < args.len; n++) {
        const IR_temporal t = P_new_temporal(p);

        // bind
        IR_SSA_binding_LIST_push(
            P_get_current_block_bindings(p),
            (IR_SSA_binding){.variable = args.array[n].name, .bind = t}
        );

        // emit VAR_DECL
        IR_typeuse* ttype = malloc(sizeof(IR_typeuse));
        *ttype = args.array[n].type;
        P_emit(p, (IR_instr){
                   .ty = IR_INST_TY__VAR_DECL,
                   .info.var_decl = {
                       .var_name = args.array[n].name,
                       .dest = t,
                       .type = ttype
                   }
               }, (IR_instr_error_info){
                   .kind = IR_IEI_variable_declaration,
                   .info.variable_declaration = {
                       .name = args.array[n].name.span,
                       // .type = ttype->full_typeuse_span,
                       // .expr = TS_merge(&args.array[n].name.span, &ttype->full_typeuse_span),
                       // .whole = TS_merge(&args.array[n].name.span, &ttype->full_typeuse_span)
                   }
               });

        // ADD TO SCOPE
        if (p->compilation_scope.len == 0) {
            P_compilation_scope_LIST_push(&p->compilation_scope, (P_compilation_scope){
                                              .variables = P_variable_record_LIST_new()
                                          });
        }
        P_variable_record_LIST_push(&P_compilation_scope_LIST_peek_last(&p->compilation_scope)->variables,
                                    (P_variable_record){
                                        .name = args.array[n].name,
                                        .type = args.array[n].type
                                    }
        );
    }

    P_get_code_section(p);

    // drop args
    if (
        p->func_scope_blocks->len > 0 &&
        p->func_scope_blocks->array[p->func_scope_blocks->len - 1].instructions.len > 0 &&
        (p->func_scope_blocks->array[p->func_scope_blocks->len - 1].instructions.array[
             p->func_scope_blocks->array[p->func_scope_blocks->len - 1].instructions.len - 1].ty == IR_INST_TY__RETURN
         ||
         p->func_scope_blocks->array[p->func_scope_blocks->len - 1].instructions.array[
             p->func_scope_blocks->array[p->func_scope_blocks->len - 1].instructions.len - 1].ty ==
         IR_INST_TY__RETURN_VOID)
    ) {
    } else {

        const IR_SSA_binding_LIST* bindings = P_get_current_block_bindings(p);
        for (size_t n = args.len; n-- > 0;) {
            const IR_typename_pair a = args.array[n];
            for (size_t b = 0; b < bindings->len; b++) {
                if (bindings->array[b].variable.identifier != a.name.identifier) continue;

                IR_typeuse* ttype = malloc(sizeof(IR_typeuse));
                *ttype = a.type;

                P_emit(p, (IR_instr){
                           .ty = IR_INST_TY__DROP,
                           .info.drop = {
                               .var_name = a.name,
                               .dest = bindings->array[b].bind,
                               .type = ttype
                           }
                       }, (IR_instr_error_info){.kind = IR_IEI_none});
                break;
            }
        }
        P_emit(p, (IR_instr) {
            .ty = IR_INST_TY__RETURN_VOID
        }, (IR_instr_error_info) { .kind = IR_IEI_none });
    }

    p->func_scope_blocks = original_blocks;

    // -------------------------------------------------------------

    const size_t new_name_size = sizeof(char)*raw_fn_name->len + 5;
    char* new_name = malloc(new_name_size);
    int ns = snprintf(new_name, new_name_size, "FN_"SV_format"", SV_p_args(raw_fn_name));

    IR_func_LIST_push(&p->functions, (IR_func){
                          .fn_ret_t = fn_ret_t,
                          .blocks = blocks,
                          .args = args,
                          .fn_name = (SpannedIdentifier){fn_name, (TokenSpan){*raw_fn_name}},
                          .raw_name = SV_from_str_len(new_name, ns),
                          .highest_temporal_number = p->temporal_fn_cnt,
                          .error_info_list = error_info_list
                      });
    printf("[info] parsed function '"SV_format"'\n", SV_p_args(raw_fn_name));


    // cleanup
    P_compilation_scope_LIST_free(&p->compilation_scope);
}

void P_get_enum_variants_defs(Parser* restrict p, IR_enumdef_variant_LIST* restrict variants,
                              const SV* restrict raw_enum_name) {
    while (1) {
        P_require_having_next_token(p, TT_EndCurlyBracket);

        if (Token_cmp(P_peek_token(p), "}")) {
            P_next_token(p);
            break;
        }

        const Token raw_variant_name = P_next_token(p);
        const Identifier variant_name = P_to_identifier(p, raw_variant_name);

        P_require_having_next_token(p, TT_EndCurlyBracket);
        const Token comma_or_vals = P_next_token(p);
        if (Token_cmp(comma_or_vals, "}") || Token_cmp(comma_or_vals, ",")) {
            IR_enumdef_variant_LIST_push(variants, (IR_enumdef_variant){
                                             .name = {variant_name, (TokenSpan){*raw_variant_name}},
                                             .values_len = 0,
                                             .values = NULL,
                                         });
        }
        if (Token_cmp(comma_or_vals, "}")) break;
        if (Token_cmp(comma_or_vals, ",")) continue;

        if (Token_cmp(comma_or_vals, "(")) {
            IR_typeuse_LIST irts = IR_typeuse_LIST_new();

            while (1) {
                IR_typeuse_LIST_push(&irts, P_get_typeuse(p, false));
                P_require_having_next_token(p, TT_Comma);
                const Token t = P_next_token(p);
                if (Token_cmp(t, ")")) {
                    break;
                }
                if (!Token_cmp(t, ",")) {
                    P_syntax_error(p, t, SV_from_str("expected either ',' or ')'"));
                    exit(1);
                }
            }

            IR_typeuse_LIST_shrink(&irts);

            IR_enumdef_variant_LIST_push(variants, (IR_enumdef_variant){
                                             .name = {variant_name, (TokenSpan){*raw_variant_name}},
                                             .values = irts.array, // res,
                                             .values_len = irts.len
                                         });

            P_require_having_next_token(p, TT_EndCurlyBracket);

            const Token end = P_next_token(p);
            if (Token_cmp(end, ",")) continue;
            if (Token_cmp(end, "}")) break;

            P_syntax_error(p, end, SV_from_str("expected either ',' or '}'"));
            exit(1);
        } else {
            P_syntax_error(p, comma_or_vals, SV_from_str("expected either ',', '(' or '}'"));
            exit(1);
        }
    }

    if (variants->len == 0) {
        P_syntax_error(p, raw_enum_name, SV_from_str("enums must have at least one variant"));
        exit(1);
    }
}

void P_get_top_enum(Parser* p) {
    const Token enum_kw = P_next_token(p);
    if (!Token_cmp(enum_kw, "enum")) {
        INTERNAL_ERROR("should be 'enum' (from calling this fn)");
    }

    P_require_having_next_token(p, TT_Identifier);
    const Token raw_enum_name = P_next_token(p);
    const Identifier enum_name = P_to_identifier(p, raw_enum_name);

    P_require_next_be(p, TT_StartCurlyBracket, SV_from_str_len("{", 1));

    IR_enumdef_variant_LIST variants = IR_enumdef_variant_LIST_new();

    P_get_enum_variants_defs(p, &variants, raw_enum_name);

    IR_enumdef_LIST_push(&p->type_table.enums, (IR_enumdef){
                             .name = {enum_name, (TokenSpan){*raw_enum_name}},
                             .variants = variants,
                         });
}

void P_get_generic_def_args(Parser* restrict p, SpannedIdentifier_LIST* restrict generic_args) {
    while (1) {
        P_require_having_next_token(p, TT_Greater);
        const Token t1 = P_peek_token(p);
        if (Token_cmp(t1, ">")) break;
        P_next_token(p);

        if (t1->len != 1) {
            goto invalid_name;
        }
        if (t1->start[0] >= 'A' && t1->start[0] <= 'Z') {
            goto valid_continue;
        }
    invalid_name: {
            P_syntax_error(p, t1, SV_from_str("generic argument must be one uppercase letter"));
            exit(1);
        }
    valid_continue: {
            const Identifier name = P_to_identifier(p, t1);
            SpannedIdentifier_LIST_push(generic_args, (SpannedIdentifier){name, (TokenSpan){*t1}});

            P_require_having_next_token(p, TT_Greater);
            const Token t2 = P_peek_token(p);
            if (Token_cmp(t2, ">")) break;
            P_next_token(p);
            if (!Token_cmp(t2, ",")) {
                P_syntax_error(p, t2, SV_from_str("expected either ',' or '>'"));
                exit(1);
            }
        }
    }
}

void P_get_top_generic_struct(Parser* p) {
    P_require_having_next_token(p, TT_Identifier);
    const Token raw_generic_name = P_next_token(p);
    const Identifier generic_name = P_to_identifier(p, raw_generic_name);

    P_require_next_be(p, TT_Less, SV_from_str_len("<", 1));

    SpannedIdentifier_LIST generic_args = SpannedIdentifier_LIST_new();

    P_get_generic_def_args(p, &generic_args);

    P_require_next_be(p, TT_Greater, SV_from_str_len(">", 1));

    P_require_next_be(p, TT_StartCurlyBracket, SV_from_str_len("{", 1));

    IR_field_LIST fields = IR_field_LIST_new();

    P_get_struct_field_list(p, &fields);

    IR_generic_structdef_LIST_push(&p->type_table.generic_structs, (IR_generic_structdef){
                                       .name = {generic_name, (TokenSpan){*raw_generic_name}},
                                       .fields = fields,
                                       .generic_args = generic_args
                                   });
}

void P_get_top_generic_enum(Parser* p) {
    P_require_having_next_token(p, TT_Identifier);
    const Token raw_generic_name = P_next_token(p);
    const Identifier generic_name = P_to_identifier(p, raw_generic_name);

    P_require_next_be(p, TT_Less, SV_from_str_len("<", 1));

    SpannedIdentifier_LIST generic_args = SpannedIdentifier_LIST_new();

    P_get_generic_def_args(p, &generic_args);

    P_require_next_be(p, TT_Greater, SV_from_str_len(">", 1));

    P_require_next_be(p, TT_StartCurlyBracket, SV_from_str_len("{", 1));


    IR_enumdef_variant_LIST variants = IR_enumdef_variant_LIST_new();

    P_get_enum_variants_defs(p, &variants, raw_generic_name);

    IR_generic_enumdef_LIST_push(&p->type_table.generic_enums, (IR_generic_enumdef){
                                     .name = {generic_name, (TokenSpan){*raw_generic_name}},
                                     .generic_args = generic_args,
                                     .variants = variants
                                 });
}

void P_get_top_extern_type(Parser* p) {
    P_require_having_next_token(p, TT_Identifier);
    const Token raw_type_name = P_next_token(p);
    const Identifier type_name = P_to_identifier(p, raw_type_name);

    SpannedIdentifier_LIST_push(&p->type_table.extern_types,
                                (SpannedIdentifier){type_name, (TokenSpan){*raw_type_name}});

    P_require_semicolon();
}

void c_type_extension_void_fail(Parser* restrict p, const CFFI_c_typeuse* restrict in, const Token t) {
    if (in->kind == CFFI_TY_VOID) {
        P_syntax_error(p, t, SV_from_str("not allowed on void, use extern type if needed"));
        exit(1);
    }
}

/// returns TRUE on success (call until false)
bool P_extend_c_typeuse(Parser* restrict p, const CFFI_c_typeuse in, CFFI_c_typeuse* restrict out) {
    P_require_having_next_token(p, TT_Star);
    const Token t = P_peek_token(p);
    if (Token_cmp(t, "*")) {
        c_type_extension_void_fail(p, &in, t);
        P_next_token(p);
        CFFI_c_typeuse* inner = malloc(sizeof(CFFI_c_typeuse));
        *inner = in;
        *out = (CFFI_c_typeuse){
            .kind = CFFI_TY_PTR,
            .inner = inner
        };
        return true;
    }

    if (Token_cmp(t, "[")) {
        c_type_extension_void_fail(p, &in, t);
        P_next_token(p);
        P_require_having_next_token(p, TT_EndSquareBracket);
        const Token v = P_next_token(p);
        if (Token_cmp(v, "]")) {
            CFFI_c_typeuse* inner = malloc(sizeof(CFFI_c_typeuse));
            *inner = in;
            *out = (CFFI_c_typeuse){
                .kind = CFFI_TY_PTR,
                .inner = inner
            };
            return true;
        }

        const int64_t array_len = EX_token_to_int(p, v);
        P_require_next_be(p, TT_EndSquareBracket, SV_from_str_len("]", 1));

        CFFI_c_typeuse* inner = malloc(sizeof(CFFI_c_typeuse));
        *inner = in;
        *out = (CFFI_c_typeuse){
            .kind = CFFI_TY_ARRAY,
            .inner = inner,
            .array_len = array_len
        };
        return true;
    }

    return false;
}

CFFI_c_typeuse P_get_c_typeuse(Parser* p, const bool allow_void) {
    P_require_having_next_token(p, TT_Identifier);

    const Token name = P_next_token(p);

    SV defaulted_type_name;
    default_type_name(name, &defaulted_type_name);

    CFFI_c_typeuse in;

    if (Token_cmp(&defaulted_type_name, "Void") || Token_cmp(&defaulted_type_name, "void")) {
        if (!allow_void) {
            P_syntax_error(p, name, SV_from_str("void as type not allowed here"));
            exit(1);
        }
        in = (CFFI_c_typeuse){
            .kind = CFFI_TY_VOID
        };
        goto end;
    }

    TC_Primitive_TY tt;
    if (TC_Primitive_TY_from_SV(&defaulted_type_name, &tt)) {
        in = (CFFI_c_typeuse){
            .kind = CFFI_TY_PRIMITIVE,
            .primitive = tt,
        };

        goto end;
    }

    const Identifier id_name = P_to_identifier(p, &defaulted_type_name);
    in = (CFFI_c_typeuse){
        .kind = CFFI_TY_NAMED,
        .named = {id_name, (TokenSpan){defaulted_type_name}}
    };
    goto end;

    UNREACHABLE();

end: {
        // wrapping
        CFFI_c_typeuse out;
        while (P_extend_c_typeuse(p, in, &out)) {
            in = out;
        }
        return out;
    }
}

/// returns true on success
/// out is allocated by this function
bool P_get_extern_fn_type_constructor(Parser* restrict p, SpannedIdentifier_LIST* restrict out) {
    P_require_having_next_token(p, TT_StartCurlyBracket);
    const Token t = P_peek_token(p);
    if (!Token_cmp(t, "{")) {
        return false;
    }
    P_next_token(p);

    SpannedIdentifier_LIST ot = SpannedIdentifier_LIST_new();

    while (1) {
        P_require_having_next_token(p, TT_EndCurlyBracket);
        const Token t1 = P_next_token(p);
        if (Token_cmp(t1, "}")) {
            break;
        }
        const Identifier arg = P_to_identifier(p, t1);
        SpannedIdentifier_LIST_push(&ot, (SpannedIdentifier){arg, (TokenSpan){*t1}});

        P_require_having_next_token(p, TT_EndCurlyBracket);
        const Token t2 = P_next_token(p);
        if (Token_cmp(t2, "}")) {
            break;
        }
        if (!Token_cmp(t2, ",")) {
            P_syntax_error(p, t2, SV_from_str("expected either ',' or ')'"));
            exit(1);
        }
    }

    *out = ot;
    return true;
}

void P_get_top_extern_fn(Parser* p) {
    const CFFI_c_typeuse c_ret_type = P_get_c_typeuse(p, true);
    P_require_having_next_token(p, TT_Identifier);
    const Token raw_c_fn_name = P_next_token(p);
    const Identifier c_fn_name = P_to_identifier(p, raw_c_fn_name);

    CFFI_c_fn_arg_LIST c_args = CFFI_c_fn_arg_LIST_new();

    P_require_next_be(p, TT_StartBracket, SV_from_str_len("(", 1));

    while (1) {
        P_require_having_next_token(p, TT_EndBracket);
        const Token f1 = P_peek_token(p);
        if (Token_cmp(f1, ")")) {
            P_next_token(p);
            break;
        }
        const CFFI_c_typeuse t = P_get_c_typeuse(p, false);
        P_require_having_next_token(p, TT_Identifier);
        const Token raw_arg = P_next_token(p);
        const Identifier arg = P_to_identifier(p, raw_arg);
        CFFI_c_fn_arg_LIST_push(&c_args, (CFFI_c_fn_arg){
                                    t,
                                    {arg, (TokenSpan){*raw_arg}}
                                });
        P_require_having_next_token(p, TT_EndBracket);
        const Token f2 = P_next_token(p);
        if (Token_cmp(f2, ")")) {
            break;
        }
        if (!Token_cmp(f2, ",")) {
            P_syntax_error(p, f2, SV_from_str("expected either ')' or ','"));
            exit(1);
        }
    }

    // now we have args

    P_require_next_be(p, TT_Identifier, SV_from_str_len("into", 4));

    const IR_typeuse ret_type = P_get_typeuse(p, true);
    SpannedIdentifier_LIST ret_type_constructor;
    const bool has_ret_type_constructor = P_get_extern_fn_type_constructor(p, &ret_type_constructor);

    P_require_having_next_token(p, TT_Identifier);
    const Token raw_fn_name = P_next_token(p);
    const Identifier fn_name = P_to_identifier(p, raw_fn_name);

    const CFFI_arg ret = (CFFI_arg){
        ret_type, {fn_name, (TokenSpan){*raw_fn_name}}, has_ret_type_constructor, ret_type_constructor
    };


    P_require_next_be(p, TT_StartBracket, SV_from_str_len("(", 1));

    CFFI_arg_LIST args = CFFI_arg_LIST_new();

    while (1) {
        P_require_having_next_token(p, TT_EndBracket);
        const Token t = P_peek_token(p);
        if (Token_cmp(t, ")")) {
            P_next_token(p);
            break;
        }

        const IR_typeuse type = P_get_typeuse(p, false);

        P_require_having_next_token(p, TT_Identifier);
        const Token raw_name = P_next_token(p);
        const Identifier name = P_to_identifier(p, raw_name);

        SpannedIdentifier_LIST type_constructor;
        const bool has_type_constructor = P_get_extern_fn_type_constructor(p, &type_constructor);

        CFFI_arg_LIST_push(&args, (CFFI_arg){
                               type, {name, (TokenSpan){*raw_name}}, has_type_constructor, type_constructor
                           });

        P_require_having_next_token(p, TT_EndBracket);
        const Token t2 = P_next_token(p);
        if (Token_cmp(t2, ")")) break;
        if (!Token_cmp(t2, ",")) {
            P_syntax_error(p, t2, SV_from_str("expected either ',' or '('"));
            exit(1);
        }
    }

    P_require_semicolon();

    const CFFI_IR_extern_fn extern_fn = (CFFI_IR_extern_fn){
        c_ret_type,
        {c_fn_name, (TokenSpan){*raw_c_fn_name}},
        c_args,

        ret,
        args,
    };

    CFFI_IR_extern_fn_LIST_push(&p->external_functions, extern_fn);
}

/// generic <type> <name> <A,B...> ( <args ) { <code> }
void P_get_top_generic_function(Parser* restrict p) {
    p->temporal_fn_cnt = 0;
    p->compilation_scope = P_compilation_scope_LIST_new();

    const IR_typeuse fn_ret_t = P_get_typeuse(p, true);

    P_require_having_next_token(p, TT_Identifier);
    const Token fn_name_raw = P_next_token(p);
    const Identifier fn_name = P_to_identifier(p, fn_name_raw);

    P_require_next_be(p, TT_Less, SV_from_str_len("<", 1));
    SpannedIdentifier_LIST generic_args = SpannedIdentifier_LIST_new();
    P_get_generic_def_args(p, &generic_args);
    P_require_next_be(p, TT_Greater, SV_from_str_len(">", 1));

    P_require_next_be(p, TT_StartBracket, SV_from_str_len("(", 1));

    IR_typename_pair_LIST args = IR_typename_pair_LIST_new();

    while (1) {
        P_require_having_next_token(p, TT_EndBracket);
        if (Token_cmp(P_peek_token(p), ")")) {
            break;
        }
        const IR_typeuse ty = P_get_typeuse(p, false);

        P_require_having_next_token(p, TT_Identifier);
        const Token pre_name_token = &p->positioner.ref->array[p->positioner.pos - 1];
        const Token name = P_next_token(p);
        if (Token_cmp(name, ",")) {
            expected_token_error(&p->source, &p->source_filename, pre_name_token, "argument name");
            exit(1);
        }

        const Identifier id_name = P_to_identifier(p, name);

        IR_typename_pair_LIST_push(&args, (IR_typename_pair){.type = ty, .name = {id_name, (TokenSpan){*name}}});

        if (Token_cmp(P_peek_token(p), ")")) {
            break;
        }
        P_require_next_be(p, TT_Comma, SV_from_str_len(",", 1));
    }


    P_require_next_be(p, TT_EndBracket, SV_from_str_len(")", 1));


    // -------------------------------------------------------------

    IR_block_LIST* original_blocks = p->func_scope_blocks;
    IR_block_LIST blocks = IR_block_LIST_new();
    p->func_scope_blocks = &blocks;

    IR_instr_error_info_LIST error_info_list = IR_instr_error_info_LIST_new();
    p->func_error_info_list = &error_info_list;

    p->fn_args = &args;

    P_append_block(p);

    for (size_t n = 0; n < args.len; n++) {
        const IR_temporal t = P_new_temporal(p);

        // bind
        IR_SSA_binding_LIST_push(
            P_get_current_block_bindings(p),
            (IR_SSA_binding){.variable = args.array[n].name, .bind = t}
        );

        // emit VAR_DECL
        IR_typeuse* ttype = malloc(sizeof(IR_typeuse));
        *ttype = args.array[n].type;
        P_emit(p, (IR_instr){
                   .ty = IR_INST_TY__VAR_DECL,
                   .info.var_decl = {
                       .var_name = args.array[n].name,
                       .dest = t,
                       .type = ttype
                   }
               }, (IR_instr_error_info){
                   .kind = IR_IEI_variable_declaration,
                   .info.variable_declaration = {
                       .name = args.array[n].name.span,
                       // .type = ttype->full_typeuse_span,
                       // .expr = TS_merge(&args.array[n].name.span, &ttype->full_typeuse_span),
                       // .whole = TS_merge(&args.array[n].name.span, &ttype->full_typeuse_span)
                   }
               });

        // ADD TO SCOPE
        if (p->compilation_scope.len == 0) {
            P_compilation_scope_LIST_push(&p->compilation_scope, (P_compilation_scope){
                                              .variables = P_variable_record_LIST_new()
                                          });
        }
        P_variable_record_LIST_push(&P_compilation_scope_LIST_peek_last(&p->compilation_scope)->variables,
                                    (P_variable_record){
                                        .name = args.array[n].name,
                                        .type = args.array[n].type
                                    }
        );
    }

    P_get_code_section(p);

    // drop args
    if (
        p->func_scope_blocks->len > 0 &&
        p->func_scope_blocks->array[p->func_scope_blocks->len - 1].instructions.len > 0 &&
        (p->func_scope_blocks->array[p->func_scope_blocks->len - 1].instructions.array[
             p->func_scope_blocks->array[p->func_scope_blocks->len - 1].instructions.len - 1].ty == IR_INST_TY__RETURN
         ||
         p->func_scope_blocks->array[p->func_scope_blocks->len - 1].instructions.array[
             p->func_scope_blocks->array[p->func_scope_blocks->len - 1].instructions.len - 1].ty ==
         IR_INST_TY__RETURN_VOID)
    ) {
    } else {
        const IR_SSA_binding_LIST* bindings = P_get_current_block_bindings(p);
        for (size_t n = args.len; n-- > 0;) {
            const IR_typename_pair a = args.array[n];
            for (size_t b = 0; b < bindings->len; b++) {
                if (bindings->array[b].variable.identifier != a.name.identifier) continue;

                IR_typeuse* ttype = malloc(sizeof(IR_typeuse));
                *ttype = a.type;

                P_emit(p, (IR_instr){
                           .ty = IR_INST_TY__DROP,
                           .info.drop = {
                               .var_name = a.name,
                               .dest = bindings->array[b].bind,
                               .type = ttype
                           }
                       }, (IR_instr_error_info){.kind = IR_IEI_none});
                break;
            }
        }
        P_emit(p, (IR_instr) {
            .ty = IR_INST_TY__RETURN_VOID
        }, (IR_instr_error_info) { .kind = IR_IEI_none });
    }

    p->func_scope_blocks = original_blocks;

    // -------------------------------------------------------------

    // IR_func_LIST_push(&p->functions, (IR_func){
    //                       .fn_ret_t = fn_ret_t,
    //                       .blocks = blocks,
    //                       .args = args,
    //                       .fn_name = (SpannedIdentifier){fn_name, (TokenSpan){*raw_fn_name}},
    //                       .highest_temporal_number = p->temporal_fn_cnt,
    //                       .error_info_list = error_info_list
    //                   });
    // printf("[info] parsed function '"SV_format"'\n", SV_p_args(raw_fn_name));
    IR_generic_function_LIST_push(&p->generic_functions, (IR_generic_function) {
        .fn = (IR_func){
            .fn_ret_t = fn_ret_t,
            .blocks = blocks,
            .args = args,
            .fn_name = (SpannedIdentifier){fn_name, (TokenSpan){*fn_name_raw}},
            .highest_temporal_number = p->temporal_fn_cnt,
            .error_info_list = error_info_list
        },
        .generic_args = generic_args
    });

    printf("[info] parsed generic function '"SV_format"'\n", SV_p_args(fn_name_raw));
    // UNIMPLEMENTED()


    // cleanup
    P_compilation_scope_LIST_free(&p->compilation_scope);
}

void P_get_top(Parser* p) {
    const Token t1 = P_peek_token(p);
    if (Token_cmp(t1, "struct")) {
        P_get_top_struct(p);
    } else if (Token_cmp(t1, "enum")) {
        P_get_top_enum(p);
    } else if (Token_cmp(t1, "generic")) {
        P_next_token(p);
        P_require_having_next_token(p, TT_Identifier);
        const Token t2 = P_peek_token(p);
        if (Token_cmp(t2, "struct")) {
            P_next_token(p);
            P_get_top_generic_struct(p);
        } else if (Token_cmp(t2, "enum")) {
            P_next_token(p);
            P_get_top_generic_enum(p);
        } else {

            // generic function
            P_get_top_generic_function(p);

            // P_syntax_error(p, t2, SV_from_str("expected either 'struct' or 'enum'"));
            // exit(1);
        }
    } else if (Token_cmp(t1, "extern")) {
        P_next_token(p);
        P_require_having_next_token(p, TT_Identifier);
        const Token t2 = P_peek_token(p);
        if (Token_cmp(t2, "type")) {
            P_next_token(p);
            P_get_top_extern_type(p);
        } else {
            P_get_top_extern_fn(p);
        }
    } else {
        P_get_top_fn(p);
    }
}


TC_TypeTable parse(
    SVList* restrict tokens,
    const SourceInfo source_info,
    const TargetInfo target_info,
    khash_t(sv_u64_map)* restrict identifier_map,
    IR_func_LIST* restrict functions,
    CFFI_IR_extern_fn_LIST* restrict external_functions
) {
    Parser p = (Parser){
        SVList_to_view(tokens),
        source_info.source,
        source_info.source_filename,
        IR_func_LIST_new(),
        identifier_map,
        1,
        NULL,
        NULL,
        0,
        (P_compilation_scope_LIST){0},
        NULL,
        (IR_incomplete_TypeTable){
            IR_structdef_LIST_new(),
            IR_enumdef_LIST_new(),
            IR_generic_structdef_LIST_new(),
            IR_generic_enumdef_LIST_new(),
            SpannedIdentifier_LIST_new(),
        },
        CFFI_IR_extern_fn_LIST_new(),
        IR_generic_function_LIST_new()
    };

    init__compiler_builtin__tc_types(p.identifier_map, &p.identifier_map_cnt, &target_info);


    // ---------------------------------------------------------------------------------

    while (P_has_next(&p)) {
        P_get_top(&p);
    }


    // ---------------------------------------------------------------------------------


    // printf("GENERICS:\n");
    // printf("  STRUCTS:\n");
    // for (size_t n=0; n<p.type_table.generic_structs.len; n++) {
    //     const IR_generic_structdef sd = p.type_table.generic_structs.array[n];
    //     const SV name = SV_from_identifiermap_lookup(p.identifier_map, sd.name);
    //     printf("    '"SV_format"'<", SV_v_args(name));
    //     for (size_t k=0;k<sd.generic_args.len;k++) {
    //         const SV generic_arg = SV_from_identifiermap_lookup(p.identifier_map, sd.generic_args.array[k]);
    //         printf(SV_format, SV_v_args(generic_arg));
    //         if (k+1 != sd.generic_args.len)
    //             printf(", ");
    //     }
    //     printf("> {\n");
    //     for (size_t j=0; j < sd.fields.len; j++) {
    //         const IR_field f = sd.fields.array[j];
    //         const SV field_name = SV_from_identifiermap_lookup(p.identifier_map, f.name);
    //         printf("      ");
    //         print_IR_typeuse(&f.type, p.identifier_map);
    //         printf(" "SV_format";\n", SV_v_args(field_name));
    //     }
    //     printf("    }\n");
    // }
    // printf("  ENUMS:\n");
    // for (size_t n=0; n<p.type_table.generic_enums.len; n++) {
    //     const IR_generic_enumdef ed = p.type_table.generic_enums.array[n];
    //     const SV name = SV_from_identifiermap_lookup(p.identifier_map, ed.name);
    //     printf("    '"SV_format"'<", SV_v_args(name));
    //     for (size_t k=0;k<ed.generic_args.len;k++) {
    //         const SV generic_arg = SV_from_identifiermap_lookup(p.identifier_map, ed.generic_args.array[k]);
    //         printf(SV_format, SV_v_args(generic_arg));
    //         if (k+1 != ed.generic_args.len)
    //             printf(", ");
    //     }
    //     printf("> {\n");
    //     for (size_t j=0; j < ed.variants.len; j++) {
    //         const IR_enumdef_variant v = ed.variants.array[j];
    //         const SV v_name = SV_from_identifiermap_lookup(p.identifier_map, v.name);
    //         printf("      "SV_format, SV_v_args(v_name));
    //         if (v.values_len != 0) {
    //             printf("(");
    //             for (size_t h=0; h<v.values_len; h++) {
    //                 print_IR_typeuse(&v.values[h], p.identifier_map);
    //                 if (h + 1 != v.values_len) {
    //                     printf(", ");
    //                 }
    //             }
    //             printf(")");
    //         }
    //         if (j + 1 != ed.variants.len) {
    //             printf(",");
    //         }
    //         printf("\n");
    //     }
    //     printf("    }\n");
    // }


    // ---------------------------------------------------------------------------------

    const TC_TypeTable nct = close_type_table(&p.type_table, p.identifier_map, p.generic_functions, &target_info, &source_info);

    *functions = p.functions;
    *external_functions = p.external_functions;

    return nct;
}
