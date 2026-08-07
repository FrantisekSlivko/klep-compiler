#include "parser.h"

#include <stdint.h>
#include <stdio.h>

#include "error.h"
#include "utils/dev.h"
#include "ir.h"

#include "khash.h"

typedef struct {
    Identifier name;
    size_t name_origin_token_index;
} P_variable_record;

init_list_type(P_variable_record, P_variable_record_LIST)

typedef struct {
    P_variable_record_LIST variables;
} P_compilation_scope;

init_list_type(P_compilation_scope, P_compilation_scope_LIST)

// typedef struct {
//     Identifier field_name;
//
// } TYPEDEF_StructField;
//
// init_list_type(TYPEDEF_StructField, TYPEDEF_StructField_LIST)
//
// typedef struct {
//     Identifier name;
//     // fields: Vec<FieldDef>,
//     TYPEDEF_StructField_LIST fields;
//     size_t size;       // computed after resolution
//     size_t align;
// } TYPEDEF_StructDef;
//
// // KHASH_MAP_INIT_INT64(structs_map, TYPEDEF_StructDef)
// init_list_type(TYPEDEF_StructDef, TYPEDEF_StructDef_LIST)

typedef struct {
    Identifier name;
    IR_typeuse type; // IR_TY_Named for structs, IR_TY_Primitive for builtins, etc.
} IR_field;

init_list_type(IR_field, IR_field_LIST)

typedef struct {
    Identifier name;
    IR_field_LIST fields;
} IR_structdef;

init_list_type(IR_structdef, IR_structdef_LIST)

typedef struct {
    IR_structdef_LIST structs;
} TypeTable;

typedef struct {
    // --- compilation process --
    SVListView positioner;
    const SV* restrict source;
    const SV* restrict source_filename;


    // global result accumulation
    IR_func_LIST functions;
    khash_t(sv_u64_map)* identifier_map;
    uint64_t identifier_map_cnt;

    // per-function code compilation
    IR_block_LIST* func_scope_blocks;
    uint64_t temporal_fn_cnt;
    P_compilation_scope_LIST compilation_scope;

    TypeTable type_table;
} Parser;

bool P_has_next(Parser* p) {
    return SVListView_has_next(&p->positioner);
}

Token P_next_token(Parser* p) {
    return SVListView_next(&p->positioner);
}

Token P_peek_token(Parser* p) {
    return SVListView_peek(&p->positioner);
}

// -------------------------------------------


void P_get_code_section(Parser* p);

// -------------------------------------------

/*
expected_token_error(p->source, p->source_filename, P_peek_token(p), "Identifier");
const SV msg = SV_from_str("hi");
syntax_error(p->source, p->source_filename, P_peek_token(p), &msg);
*/

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
} TokenType;

char* TT_to_expected(const TokenType t) {
    char* expected = 0;
    switch (t) {
        case TT_Identifier:
            expected = "Identifier";
            break;
        case TT_Type:
            expected = "Type";
            break;
        case TT_StartBracket:
            expected = "(";
            break;
        case TT_EndBracket:
            expected = ")";
            break;
        case TT_Comma:
            expected = ",";
            break;
        case TT_StartCurlyBracket:
            expected = "{";
            break;
        case TT_EndCurlyBracket:
            expected = "}";
            break;
        case TT_Less:
            expected = "<";
            break;
        case TT_Greater:
            expected = ">";
            break;
        case TT_Semicolon:
            expected = ";";
            break;
        case TT_Equal:
            expected = "=";
            break;
        case TT_StartSquareBracket:
            expected = "[";
            break;
        case TT_EndSquareBracket:
            expected = "]";
            break;
    }
    return expected;
}

static inline void P_syntax_error(const Parser* restrict p, const Token restrict t, SV msg) {
    syntax_error(p->source, p->source_filename, t, &msg);
}

void P_require_having_next_token(Parser* p, const TokenType t) {
    if (P_has_next(p)) return;

    const char* expected = TT_to_expected(t);

    const Token last_token = &p->positioner.ref->array[p->positioner.pos - 1];
    expected_token_error(p->source, p->source_filename, last_token, expected);
    exit(1);
}

void P_require_next_be(Parser* p, const TokenType t, const SV cmp) {
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
}

// -------------------------------------------


Identifier P_to_identifier(Parser* restrict p, const Token restrict t) {
    const char c1 = *t->start;
    if (!(('a' <= c1 && c1 <= 'z') || ('A' <= c1 && c1 <= 'Z'))) {
        const SV msg = SV_from_str("expected 'Identifier'");
        P_syntax_error(p, t, msg);
        exit(1);
    }
    const khiter_t kf = kh_get(sv_u64_map, p->identifier_map, *t);
    if (kf != kh_end(p->identifier_map)) {
        return kh_value(p->identifier_map, kf); // found
    }

    int ret;
    const khiter_t k = kh_put(sv_u64_map, p->identifier_map, *t, &ret);
    const Identifier id = p->identifier_map_cnt++;
    kh_value(p->identifier_map, k) = id;
    return id;
}

SV SV_from_identifiermap_lookup(khash_t(sv_u64_map)* identifier_map, const uint64_t identifier) {
    for (khint_t k = kh_begin(identifier_map); k != kh_end(identifier_map); ++k) {
        if (!kh_exist(identifier_map, k)) continue;

        const SV key = kh_key(identifier_map, k);
        const uint64_t val = kh_value(identifier_map, k);
        if (val != identifier) {
            continue;
        }

        return key;
    }
    fprintf(stderr, "invalid Identifier lookup\n");
    exit(3);
}


IR_typeuse P_get_typeuse(Parser* p);

IR_typeuse P_get_typeuse_intern_generic(Parser* p, const Token name, IR_typeuse_type ty) {
    P_require_next_be(p, TT_Less, SV_from_str_len("<", 1));

    IR_typeuse* irt = malloc(sizeof(IR_typeuse));
    *irt = P_get_typeuse(p);

    P_require_next_be(p, TT_Greater, SV_from_str_len(">", 1));

    const Identifier id_name = P_to_identifier(p, name);

    return (IR_typeuse){
        .name = id_name,
        .tt_ty = ty,
        .contained = irt,
    };
}

IR_typeuse P_get_typeuse(Parser* p) {
    const Token name = P_next_token(p);
    const SV int_repl = SV_from_str_len("Int32", 5);
    const Identifier id_name = P_to_identifier(p, Token_cmp(name, "Int") ? &int_repl : name);

    if (IR_TY_is(name)) {
        return (IR_typeuse){.name = id_name, .tt_ty = IR_TY_Primitive};
    }

    if (Token_cmp(name, "Ref")) {
        return P_get_typeuse_intern_generic(p, name, IR_TY_Ref);
    }

    if (Token_cmp(name, "Array")) {
        return P_get_typeuse_intern_generic(p, name, IR_TY_Array);
    }

    if (Token_cmp(name, "List")) {
        return P_get_typeuse_intern_generic(p, name, IR_TY_List);
    }

    return (IR_typeuse){.name = id_name, .tt_ty = IR_TY_Named};
}

// -------------------------------------------

/// returns current block's index
size_t P_get_current_block_index(const Parser* p) {
    return p->func_scope_blocks->len - 1;
}

bool P_emit_block_is_empty(const Parser* p) {
    return p->func_scope_blocks->array[p->func_scope_blocks->len - 1].instructions.len == 0;
}

void P_emit_to_block(const Parser* p, const size_t block_nth, const IR_instr instr) {
    if (block_nth >= p->func_scope_blocks->len) {
        fprintf(stderr, "[intern] invalid block position (not that many blocks)\n");
        exit(3);
    }
    IR_block* block = &p->func_scope_blocks->array[block_nth];
    IR_instr_LIST_push(&block->instructions, instr);
}

void P_emit_to_block_start(const Parser* p, const size_t block_nth, const IR_instr instr) {
    if (block_nth >= p->func_scope_blocks->len) {
        fprintf(stderr, "[intern] invalid block position (not that many blocks)\n");
        exit(3);
    }
    IR_block* block = &p->func_scope_blocks->array[block_nth];
    IR_instr_LIST_push(&block->instructions, instr); // used to make space

    memmove(&block->instructions.array[1], &block->instructions.array[0],
            (block->instructions.len - 1) * sizeof(IR_instr));
    block->instructions.array[0] = instr;
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

void P_emit(const Parser* p, const IR_instr instr) {
    if (p->func_scope_blocks->len == 0) {
        P_append_block(p);
    }
    IR_block* last = &p->func_scope_blocks->array[p->func_scope_blocks->len - 1];
    IR_instr_LIST_push(&last->instructions, instr);
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

bool EX_is_OP(const Token op) {
    // printf("OP is '"SV_format"'\n", SV_p_args(op));
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

    // if (op->len == 2) {
    const char c1 = op->start[0];
    const char c2 = op->start[1];
    return
            (c1 == '=' && c2 == '=') ||
            (c1 == '<' && c2 == '=') ||
            (c1 == '>' && c2 == '=') ||
            (c1 == 'o' && c2 == 'r');
    // }
    // return false;
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
                fprintf(stderr, "[intern] invalid OP '%c' (%u)\n", op->start[0], op->start[0]);
                exit(3);
        }
    }
    if (op->len == 2) {
        const char c1 = op->start[0];
        const char c2 = op->start[1];
        if (c1 == '=' && c2 == '=') {
            return P_OP__CMP_EQ;
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
    fprintf(stderr, "[intern] invalid OP '"SV_format"'\n", SV_p_args(op));
    exit(3);
}

uint8_t EX_precedence(const P_OP op) {
    switch (op) {
        case P_OP__OR:
            return 10;
        case P_OP__AND:
            return 15;
        case P_OP__CMP_EQ:
            return 20;

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
    fprintf(stderr, "[intern] invalid EX_precedence control flow ('%u')\n", op);
    exit(3);
}

bool EX_is_op_right_assoc(const P_OP op) {
    return op == P_OP__POW; // ^
}

IR_temporal P_get_expr(Parser* p);

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

IR_temporal P_expr_accesors_wrapper(Parser* p, const IR_temporal origin) {
    P_require_having_next_token(p, TT_Semicolon);
    const Token nx = P_peek_token(p);

    if (nx->len == 1 && nx->start[0] == '.') {
        P_next_token(p); // dot
        if (!P_has_next(p)) {
            expected_token_error(p->source, p->source_filename, nx, "field name");
            exit(1);
        }
        const Token field_name = P_next_token(p);
        const Identifier i = P_to_identifier(p, field_name);

        const IR_temporal result = P_new_temporal(p);

        P_emit(p, (IR_instr){
                   .ty = IR_INST_TY__GET_FIELD_PTR,
                   .dest = result,
                   .p1.name = i,
                   .p2.src2 = origin,
               });

        return P_expr_accesors_wrapper(p, result);
    }

    if (nx->len == 1 && nx->start[0] == '[') {
        P_next_token(p); // [
        const IR_temporal index = P_get_expr(p);
        P_require_next_be(p, TT_EndSquareBracket, SV_from_str("]"));

        const IR_temporal result = P_new_temporal(p);

        P_emit(p, (IR_instr){
                   .ty = IR_INST_TY__INDEX_PTR,
                   .dest = result,
                   .p1.src1 = origin,
                   .p2.src2 = index,
               });

        return P_expr_accesors_wrapper(p, result);
    }


    return origin;
}

IR_temporal P_variable_lookup(const Parser* restrict p, const Token restrict name, const Identifier name2) {
    IR_temporal dest = {.val = 0};
    bool found_temp = false;
    // lookup bindings
    const IR_SSA_binding_LIST* bindings = P_get_current_block_bindings(p);
    for (size_t n = 0; n < bindings->len; n++) {
        if (bindings->array[n].variable == name2) {
            found_temp = true;
            dest = bindings->array[n].bind;
        }
    }
    // lookup scope if not in bindings
    if (!found_temp) {
        P_syntax_error(p, name, SV_from_str("unknown variable"));
        exit(1);
    }
    return dest;
}

/// todo: incomplete
/// handles NUM, VAR, (EXPR), [EXPR, EXPR, ...], FN_CALL()
/// {VAR, (EXPR), FN_CALL()}.accessors
IR_temporal P_get_expr_val(Parser* p) {
    const Token t = P_next_token(p);

    if (Token_cmp(t, "(")) {
        // (EXPR)
        const IR_temporal rs = P_get_expr(p);
        P_require_next_be(p, TT_EndBracket, SV_from_str(")"));

        const IR_temporal wrapped_result = P_expr_accesors_wrapper(p, rs);
        if (wrapped_result.val != rs.val) {
            const IR_temporal val_load = P_new_temporal(p);
            P_emit(p, (IR_instr){
                       .ty = IR_INST_TY__READ_PTR,
                       .dest = val_load,
                       .p1.src1 = wrapped_result,
                   });
            return val_load;
        } else {
            return wrapped_result;
        }
    }

    if (Token_cmp(t, "[")) {
        // array
        P_require_having_next_token(p, TT_EndSquareBracket);
        const Token t1 = P_peek_token(p);

        IR_temporal_LIST* items = malloc(sizeof(IR_temporal_LIST));
        *items = IR_temporal_LIST_new();


        if (Token_cmp(t1, "]")) {
            P_next_token(p);
            const IR_temporal dest = P_new_temporal(p);
            P_emit(p, (IR_instr){
                       .ty = IR_INST_TY__MAKE_ARRAY,
                       .dest = dest,
                       .p2 = {.array = items}
                   });
            return dest;
        }

        while (1) {
            P_require_having_next_token(p, TT_EndSquareBracket);
            const Token tok = P_peek_token(p);
            if (Token_cmp(tok, "]")) {
                P_next_token(p);
                break;
            }

            const IR_temporal expr = P_get_expr(p);
            IR_temporal_LIST_push(items, expr);

            P_require_having_next_token(p, TT_EndSquareBracket);
            const Token end = P_next_token(p);
            if (Token_cmp(end, "]")) {
                break;
            }

            if (Token_cmp(end, ",")) {
                continue;
            }
            P_syntax_error(p, end, SV_from_str("expected ',' or ']'"));
        }

        const IR_temporal dest = P_new_temporal(p);

        P_emit(p, (IR_instr){
                   .ty = IR_INST_TY__MAKE_ARRAY,
                   .dest = dest,
                   .p2 = {.array = items}
               });
        return dest;
    }

    // name
    if (('a' <= t->start[0] && t->start[0] <= 'z') || ('A' <= t->start[0] && t->start[0] <= 'Z')) {
        // const IR_temporal dest = P_new_temporal(p);

        const Identifier name = P_to_identifier(p, t);

        IR_temporal dest = {.val = 0};
        if (P_has_next(p)) {
            const Token nx = P_peek_token(p);
            if (Token_cmp(nx, "(")) {
                // fn call
                P_next_token(p); // (
                IR_temporal_LIST* args = malloc(sizeof(IR_temporal_LIST));
                *args = IR_temporal_LIST_new();

                while (1) {
                    P_require_having_next_token(p, TT_EndBracket);
                    const Token kx = P_peek_token(p);
                    if (Token_cmp(kx, ")")) {
                        P_next_token(p);
                        break;
                    }

                    const IR_temporal expr = P_get_expr(p);
                    IR_temporal_LIST_push(args, expr);

                    P_require_having_next_token(p, TT_EndBracket);
                    const Token jx = P_next_token(p);
                    if (Token_cmp(jx, ")")) {
                        break;
                    }

                    if (!Token_cmp(jx, ",")) {
                        P_syntax_error(p, jx, SV_from_str("expected either ',' or ')'"));
                        exit(1);
                    }
                }

                const IR_temporal fn_call = P_new_temporal(p);

                P_emit(p, (IR_instr){
                           .ty = IR_INST_TY__FN_CALL_RET,
                           .dest = fn_call,
                           .p1.name = name,
                           .p2.array = args,
                       });

                dest = fn_call;
            } else {
                dest = P_variable_lookup(p, t, name);
            }
        } else {
            dest = P_variable_lookup(p, t, name);
        }


        const IR_temporal wrapped_result = P_expr_accesors_wrapper(p, dest);
        if (wrapped_result.val != dest.val) {
            const IR_temporal val_load = P_new_temporal(p);
            P_emit(p, (IR_instr){
                       .ty = IR_INST_TY__READ_PTR,
                       .dest = val_load,
                       .p1.src1 = wrapped_result,
                   });
            return val_load;
        } else {
            return wrapped_result;
        }
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

    if (is_float) {
        float64_t val;
        if (Token_cmp(t, "-")) {
            P_require_having_next_token(p, TT_Identifier);
            const Token t2 = P_next_token(p);
            val = -EX_token_to_float(p, t2);
        } else {
            val = EX_token_to_float(p, t);
        }

        const IR_temporal res = P_new_temporal(p);

        P_emit(p, (IR_instr){
                   .ty = IR_INST_TY__CONST_FLOAT,
                   .dest = res,
                   .p1.value_float = val,
               });

        return res;
    } else {
        int64_t val;
        if (Token_cmp(t, "-")) {
            P_require_having_next_token(p, TT_Identifier);
            const Token t2 = P_next_token(p);
            val = -EX_token_to_int(p, t2);
        } else {
            val = EX_token_to_int(p, t);
        }

        const IR_temporal res = P_new_temporal(p);

        P_emit(p, (IR_instr){
                   .ty = IR_INST_TY__CONST_INT,
                   .dest = res,
                   .p1.value = val,
               });

        return res;
    }
}

/// returns IR handle to value that the expr produced
IR_temporal P_get_expr(Parser* p) {
    P_OP_LIST ops = P_OP_LIST_new();
    IR_temporal_LIST temps = IR_temporal_LIST_new();


    const IR_temporal start_val = P_get_expr_val(p);
    IR_temporal_LIST_push(&temps, start_val);

    bool get_v = false;
    while (1) {
        if (get_v) {
            const IR_temporal val = P_get_expr_val(p);
            IR_temporal_LIST_push(&temps, val);
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

                    const IR_temporal v1 = *IR_temporal_LIST_pop(&temps);
                    const IR_temporal v2 = *IR_temporal_LIST_pop(&temps);

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
                               .dest = dest,
                               .ty = ty,
                               .p1 = {.src1 = v2},
                               .p2 = {.src2 = v1},
                           });

                    IR_temporal_LIST_push(&temps, dest);
                }
                P_OP_LIST_push(&ops, item);
            } else {
                break;
            }
        }
        get_v = !get_v;
    }
    while (!P_OP_LIST_is_empty(&ops)) {
        const P_OP op = *P_OP_LIST_pop(&ops);

        const IR_temporal v1 = *IR_temporal_LIST_pop(&temps);
        const IR_temporal v2 = *IR_temporal_LIST_pop(&temps);

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
                   .dest = dest,
                   .ty = ty,
                   .p1 = {.src1 = v2},
                   .p2 = {.src2 = v1},
               });

        IR_temporal_LIST_push(&temps, dest);
    }

    const IR_temporal last = *IR_temporal_LIST_pop(&temps); // safe since deref

    P_OP_LIST_free(&ops);
    IR_temporal_LIST_free(&temps);
    return last;
}


void P_get_var_dec_stmt(Parser* p) {
    IR_typeuse* ty = malloc(sizeof(IR_typeuse));
    *ty = P_get_typeuse(p);

    P_require_having_next_token(p, TT_Identifier);
    const size_t name_token_pos = p->positioner.pos;
    const Token raw_name = P_next_token(p);
    const Identifier name = P_to_identifier(p, raw_name);

    // do a scope check
    for (size_t n = p->compilation_scope.len; n > 0; n--) {
        const P_compilation_scope* scope = &p->compilation_scope.array[n - 1];

        for (size_t k = 0; k < scope->variables.len; k++) {
            const P_variable_record* var = &scope->variables.array[k];
            if (var->name == name) {
                P_syntax_error(p, &p->positioner.ref->array[var->name_origin_token_index],
                               SV_from_str("declared here"));
                P_syntax_error(p, raw_name, SV_from_str("redeclaration"));
                exit(1);
            }
        }
    }

    // insert into scope
    P_variable_record_LIST_push(&P_compilation_scope_LIST_peek_last(&p->compilation_scope)->variables,
                                (P_variable_record){
                                    .name = name,
                                    .name_origin_token_index = name_token_pos,
                                });

    P_require_next_be(p, TT_Equal, SV_from_str_len("=", 1));

    const IR_temporal rs = P_get_expr(p);

    P_emit(p, (IR_instr){
               .ty = IR_INST_TY__VAR_DECL,
               .p1 = {.name = name},
               .p2 = {.type = ty}
           });

    // use bindings instead
    // P_emit(p, (IR_instr) {
    //     .ty = IR_INST_TY__STORE_VAR,
    //     .dest = (IR_temporal) { .val = name },
    //     .p1 = { .src1 = rs },
    // });
    IR_SSA_binding_LIST* binds = P_get_current_block_bindings(p);
    bool bind_find = false;
    for (size_t n = 0; n < binds->len; n++) {
        if (binds->array[n].variable == name) {
            bind_find = true;
            binds->array[n].bind = rs;
        }
    }
    if (!bind_find) {
        IR_SSA_binding_LIST_push(binds, (IR_SSA_binding){
                                     .variable = name,
                                     .bind = rs
                                 });
    }


    P_require_next_be(p, TT_Semicolon, SV_from_str_len(";", 1));
}

IR_SSA_binding_LIST P_PHI_CONDITIONAL_BRANCHING_MERGER(Parser* restrict p, const IR_SSA_binding_LIST* restrict a,
                                                       const size_t a_block_id, const IR_SSA_binding_LIST* restrict b,
                                                       const size_t b_block_id) {
    IR_SSA_binding_LIST out = IR_SSA_binding_LIST_new();
    for (size_t n_a = 0; n_a < a->len; n_a++) {
        const IR_SSA_binding a_val = a->array[n_a];
        for (size_t n_b = 0; n_b < b->len; n_b++) {
            const IR_SSA_binding b_val = b->array[n_b];
            if (a_val.variable == b_val.variable) {
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
                               .dest = phi,
                               .p2.phi_args = args,
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
        fprintf(stderr, "[internal] should be 'if' (from calling this fn)\n");
        exit(3);
    }

    const IR_temporal cmp_rs = P_get_expr(p);
    const size_t block_with_cmp = P_get_current_block_index(p);
    const IR_SSA_binding_LIST cmp_bindings = *P_get_current_block_bindings(p);

    // P_append_block(p);
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
            // P_append_block_with_bindings(p, IR_SSA_bindings_deep_clone(&cmp_bindings));
            else_section_start_block = P_get_current_block_index(p);
            else_section_end_block = P_get_current_block_index(p);
            after_block = P_get_current_block_index(p);

            // PHI MERGER if+cmp blocks
            const IR_SSA_binding_LIST res = P_PHI_CONDITIONAL_BRANCHING_MERGER(
                p, &cmp_bindings, block_with_cmp, &if_bindings, if_section_end_block);
            IR_SSA_binding_LIST* cur = P_get_current_block_bindings(p);
            IR_SSA_binding_LIST_free(cur);
            *cur = res;

            goto end;
        }

        P_next_token(p); // else
        P_require_having_next_token(p, TT_StartCurlyBracket);
        const Token nx2 = P_peek_token(p);
        if (Token_cmp(nx2, "{")) {
            // P_append_block(p);
            P_append_block_with_bindings(p, IR_SSA_bindings_deep_clone(&cmp_bindings));
            else_section_start_block = P_get_current_block_index(p);
            P_get_code_section(p);
            else_section_end_block = P_get_current_block_index(p);

            const IR_SSA_binding_LIST else_bindings = *P_get_current_block_bindings(p);

            // if (!P_emit_block_is_empty(p)) {
            P_append_block(p);
            // }
            after_block = P_get_current_block_index(p);

            // todo: unsable
            // todo: WHUT? fix // todo: PHI MERGER
            const IR_SSA_binding_LIST res = P_PHI_CONDITIONAL_BRANCHING_MERGER(
                p, &if_bindings, if_section_end_block, &else_bindings, else_section_end_block);
            IR_SSA_binding_LIST* cur = P_get_current_block_bindings(p);
            IR_SSA_binding_LIST_free(cur);
            *cur = res;
        } else if (Token_cmp(nx2, "if")) {
            // P_append_block(p);
            P_append_block_with_bindings(p, IR_SSA_bindings_deep_clone(&cmp_bindings));
            else_section_start_block = P_get_current_block_index(p);

            P_get_if_stmt(p);

            else_section_end_block = P_get_current_block_index(p);
            const IR_SSA_binding_LIST else_bindings = *P_get_current_block_bindings(p);

            // if (!P_emit_block_is_empty(p)) {
            P_append_block(p);
            // }
            after_block = P_get_current_block_index(p);

            // todo: WHUT? fix // todo: PHI MERGER
            const IR_SSA_binding_LIST res = P_PHI_CONDITIONAL_BRANCHING_MERGER(
                p, &if_bindings, if_section_end_block, &else_bindings, else_section_end_block);
            IR_SSA_binding_LIST* cur = P_get_current_block_bindings(p);
            IR_SSA_binding_LIST_free(cur);
            *cur = res;
        } else {
            // error
            P_syntax_error(p, nx2, SV_from_str("expected 'if' or '{'"));
            exit(1);
        }
    } else {
        /*// P_append_block(p);
        P_append_block_with_bindings(p, IR_SSA_bindings_deep_clone(&cmp_bindings));
        else_section_start_block = P_emit_pos_get(p);
        else_section_end_block = P_emit_pos_get(p);
        after_block = P_emit_pos_get(p);*/
        expected_token_error(p->source, p->source_filename, &p->positioner.ref->array[p->positioner.ref->len - 1],
                             "function end");
        exit(1);
    }

end:


    P_emit_to_block(p, block_with_cmp, (IR_instr){
                        .ty = IR_INST_TY__BRANCH,
                        .dest = cmp_rs,
                        .p1.br_true = if_section_start_block,
                        .p2.br_false = else_section_start_block,
                    });

    P_emit_to_block(p, if_section_end_block, (IR_instr){
                        .ty = IR_INST_TY__JUMP,
                        .p1.br_true = after_block,
                    });

    if (else_section_end_block != after_block) {
        P_emit_to_block(p, else_section_end_block, (IR_instr){
                            .ty = IR_INST_TY__JUMP,
                            .p1.br_true = after_block,
                        });
    }
}

void P_get_while_stmt(Parser* p) {
    const Token while_kw = P_next_token(p);
    if (!Token_cmp(while_kw, "while")) {
        fprintf(stderr, "[internal] should be 'while' (from calling this fn)\n");
        exit(3);
    }

    const IR_SSA_binding_LIST pre_header_bindings = *P_get_current_block_bindings(p);
    const size_t pre_header_block = P_get_current_block_index(p);

    P_append_block(p); // header

    const size_t header_block_start = P_get_current_block_index(p);

    P_emit_to_block(p, pre_header_block, (IR_instr){
                        .ty = IR_INST_TY__JUMP,
                        .p1.br_true = header_block_start
                    });

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


    const IR_temporal condition = P_get_expr(p);
    const size_t header_block_end = P_get_current_block_index(p);

    P_append_block_with_bindings(p, IR_SSA_bindings_deep_clone(&header_temp_bindings)); // body start
    const size_t body_blocks_start = P_get_current_block_index(p);
    P_get_code_section(p);
    const size_t body_blocks_end = P_get_current_block_index(p);

    P_emit(p, (IR_instr){
               .ty = IR_INST_TY__JUMP,
               .p1.br_true = header_block_start,
           });

    const IR_SSA_binding_LIST block_end_bindings = *P_get_current_block_bindings(p);


    // now resolve phis
    for (size_t n = 0; n < block_end_bindings.len; n++) {
        for (size_t k = 0; k < header_temp_bindings.len; k++) {
            if (block_end_bindings.array[n].variable == header_temp_bindings.array[k].variable) {
                IR_temporal intake_temporal = {.val = 0};
                bool intake_temporal_found = false;
                for (size_t l = 0; l < pre_header_bindings.len; l++) {
                    if (pre_header_bindings.array[n].variable == header_temp_bindings.array[k].variable) {
                        intake_temporal = pre_header_bindings.array[n].bind;
                        intake_temporal_found = true;
                        break;
                    }
                }
                if (!intake_temporal_found) {
                    fprintf(stderr, "[internal] binding disappeared 1n | while SSA phi post calc\n");
                    exit(3);
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
                                    for (size_t sn = 0; sn < instr->p2.phi_args->len; sn++) {
                                        const IR_temporal_block_phi_record val = instr->p2.phi_args->array[sn];
                                        if (val.temp.val == what_to_replace.val) {
                                            instr->p2.phi_args->array[sn].temp.val = replace_with.val;
                                        }
                                    }
                                    break;
                                }
                                case IR_INST_TY__VAR_DECL:
                                    // SHOULD NOT BE HAVE ANYTHING TO REPLACE
                                    break;
                                case IR_INST_TY__STORE_PTR: {
                                    if (instr->p1.src1.val == what_to_replace.val) {
                                        instr->p1.src1.val = replace_with.val;
                                    }
                                    break;
                                }
                                case IR_INST_TY__CONST_INT:
                                    break;
                                case IR_INST_TY__CONST_FLOAT:
                                    break;
                                case IR_INST_TY__MAKE_ARRAY: {
                                    for (size_t sn = 0; sn < instr->p2.array->len; sn++) {
                                        if (instr->p2.array->array[sn].val == what_to_replace.val) {
                                            instr->p2.array->array[sn].val = replace_with.val;
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
                                case IR_INST_TY__OP_CMP_LESS:
                                case IR_INST_TY__OP_CMP_LESS_EQ:
                                case IR_INST_TY__OP_CMP_GREATER:
                                case IR_INST_TY__OP_CMP_GREATER_EQ: {
                                    if (instr->p1.src1.val == what_to_replace.val) {
                                        instr->p1.src1.val = replace_with.val;
                                    }
                                    if (instr->p2.src2.val == what_to_replace.val) {
                                        instr->p2.src2.val = replace_with.val;
                                    }
                                    break;
                                }
                                case IR_INST_TY__BRANCH:
                                    if (instr->dest.val == what_to_replace.val) {
                                        instr->dest.val = replace_with.val;
                                    }
                                    break;
                                case IR_INST_TY__JUMP:
                                    break;
                                case IR_INST_TY__RETURN_VOID:
                                    break;
                                case IR_INST_TY__RETURN:
                                    if (instr->p1.src1.val == what_to_replace.val) {
                                        instr->p1.src1.val = replace_with.val;
                                    }
                                    break;
                                case IR_INST_TY__GET_FIELD_PTR: {
                                    if (instr->p2.src2.val == what_to_replace.val) {
                                        instr->p2.src2.val = replace_with.val;
                                    }
                                    break;
                                }
                                case IR_INST_TY__INDEX_PTR: {
                                    if (instr->p1.src1.val == what_to_replace.val) {
                                        instr->p1.src1.val = replace_with.val;
                                    }
                                    if (instr->p2.src2.val == what_to_replace.val) {
                                        instr->p2.src2.val = replace_with.val;
                                    }
                                    break;
                                }
                                case IR_INST_TY__READ_PTR: {
                                    if (instr->p1.src1.val == what_to_replace.val) {
                                        instr->p1.src1.val = replace_with.val;
                                    }
                                    break;
                                }
                                case IR_INST_TY__FN_CALL_VOID:
                                case IR_INST_TY__FN_CALL_RET: {
                                    for (size_t sn = 0; sn < instr->p2.array->len; sn++) {
                                        if (instr->p2.array->array[sn].val == what_to_replace.val) {
                                            instr->p2.array->array[sn].val = replace_with.val;
                                        }
                                    }
                                    break;
                                }
                            }
                        }
                    }
                    //
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
                                              .dest = header_temp_bindings.array[k].bind,
                                              .p2.phi_args = args,
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
                        .dest = condition,
                        .p1.br_true = body_blocks_start,
                        .p2.br_false = exit_block_start,
                    });
}


void P_get_return_stmt(Parser* p) {
    const Token ret_kw = P_next_token(p);
    if (!Token_cmp(ret_kw, "return")) {
        fprintf(stderr, "[internal] should be 'return' (from calling this fn)\n");
        exit(3);
    }

    P_require_having_next_token(p, TT_Semicolon);
    const Token nx = P_peek_token(p);
    if (Token_cmp(nx, ";")) {
        P_next_token(p);
        P_emit(p, (IR_instr){
                   .ty = IR_INST_TY__RETURN_VOID,
               });
        return;
    }

    const IR_temporal ret = P_get_expr(p);
    P_emit(p, (IR_instr){
               .ty = IR_INST_TY__RETURN,
               .p1.src1 = ret,
           });

    P_require_next_be(p, TT_Semicolon, SV_from_str_len(";", 1));
}

void P_get_assignment(Parser* p) {
    const Token origin_name = P_next_token(p);
    const Identifier name = P_to_identifier(p, origin_name);

    const IR_temporal dest = P_variable_lookup(p, origin_name, name);

    const IR_temporal wrapped_result = P_expr_accesors_wrapper(p, dest);
    const bool is_dest_ptr = dest.val != wrapped_result.val;

    P_require_having_next_token(p, TT_Equal);
    const Token assignment_type = P_next_token(p);

    const IR_temporal expr = P_get_expr(p);

    P_require_next_be(p, TT_Semicolon, SV_from_str_len(";", 1));

    IR_temporal out_val = expr;

    if (Token_cmp(assignment_type, "+=")) {
        const IR_temporal rs = P_new_temporal(p);
        P_emit(p, (IR_instr){
                   .ty = IR_INST_TY__OP_ADD,
                   .dest = rs,
                   .p1.src1 = wrapped_result,
                   .p2.src2 = expr,
               });
        out_val = rs;
    } else if (Token_cmp(assignment_type, "-=")) {
        const IR_temporal rs = P_new_temporal(p);
        P_emit(p, (IR_instr){
                   .ty = IR_INST_TY__OP_SUB,
                   .dest = rs,
                   .p1.src1 = wrapped_result,
                   .p2.src2 = expr,
               });
        out_val = rs;
    } else if (Token_cmp(assignment_type, "*=")) {
        const IR_temporal rs = P_new_temporal(p);
        P_emit(p, (IR_instr){
                   .ty = IR_INST_TY__OP_MUL,
                   .dest = rs,
                   .p1.src1 = wrapped_result,
                   .p2.src2 = expr,
               });
        out_val = rs;
    } else if (Token_cmp(assignment_type, "/=")) {
        const IR_temporal rs = P_new_temporal(p);
        P_emit(p, (IR_instr){
                   .ty = IR_INST_TY__OP_DIV,
                   .dest = rs,
                   .p1.src1 = wrapped_result,
                   .p2.src2 = expr,
               });
        out_val = rs;
    } else if (Token_cmp(assignment_type, "=")) {
        // nothing
    } else {
        P_syntax_error(p, assignment_type, SV_from_str("expected one of '=', '+=', '-=', '*=', or '/='"));
        exit(1);
    }

    if (is_dest_ptr) {
        P_emit(p, (IR_instr){
                   .ty = IR_INST_TY__STORE_PTR,
                   .dest = wrapped_result,
                   .p1.src1 = out_val,
               });
    } else {
        // P_emit(p, (IR_instr) {
        //     .ty = IR_INST_TY__STORE_VAR,
        //     .dest = (IR_temporal) { .val = name },
        //     .p1.src1 = out_val,
        // });
        IR_SSA_binding_LIST* binds = P_get_current_block_bindings(p);
        bool found = false;
        for (size_t n = 0; n < binds->len; n++) {
            if (binds->array[n].variable == name) {
                found = true;
                binds->array[n].bind = out_val;
                break;
            }
        }
        if (!found) {
            IR_SSA_binding_LIST_push(binds, (IR_SSA_binding){
                                         .variable = name,
                                         .bind = out_val,
                                     });
        }
    }
}


void P_get_function_call_stmt(Parser* p) {
    P_require_having_next_token(p, TT_Identifier);
    const Token t = P_next_token(p);
    const Identifier name = P_to_identifier(p, t);

    P_require_having_next_token(p, TT_StartBracket);
    const Token nx = P_next_token(p);
    if (!Token_cmp(nx, "(")) {
        fprintf(stderr, "[internal] expected '(' based on fn_call_stmt being called\n");
        exit(3);
    }

    IR_temporal_LIST* args = malloc(sizeof(IR_temporal_LIST));
    *args = IR_temporal_LIST_new();

    while (1) {
        P_require_having_next_token(p, TT_EndBracket);
        const Token kx = P_peek_token(p);
        if (Token_cmp(kx, ")")) {
            P_next_token(p);
            break;
        }

        const IR_temporal expr = P_get_expr(p);
        IR_temporal_LIST_push(args, expr);

        P_require_having_next_token(p, TT_EndBracket);
        const Token jx = P_next_token(p);
        if (Token_cmp(jx, ")")) {
            break;
        }

        if (!Token_cmp(jx, ",")) {
            P_syntax_error(p, jx, SV_from_str("expected either ',' or ')'"));
            exit(1);
        }
    }


    P_emit(p, (IR_instr){
        .ty = IR_INST_TY__FN_CALL_VOID,
        .p1.name = name,
        .p2.array = args,
    });

    P_require_next_be(p, TT_Semicolon, SV_from_str_len(";", 1));

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

    // either variable declaration or assignment
    // do a two token lookahead
    const size_t p_pos = p->positioner.pos;

    P_next_token(p); // const Token t1 =
    const Token t2 = P_peek_token(p);

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

    // FEATURE REDACTED
    // if (Token_cmp(t2, "++") || Token_cmp(t2, "--")) {
    //     UNIMPLEMENTED("variable inc/dec stmt");
    //     return;
    // }

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
    P_variable_record_LIST_free(&pop->variables);

    P_require_next_be(p, TT_EndCurlyBracket, SV_from_str_len("}", 1));
}

IR_block_LIST P_get_isolated_function_code_section(Parser* p) {
    IR_block_LIST* original_blocks = p->func_scope_blocks;
    IR_block_LIST blocks = IR_block_LIST_new();
    p->func_scope_blocks = &blocks;

    P_get_code_section(p);

    p->func_scope_blocks = original_blocks;
    return blocks;
}

// -------------------------------------------

void P_get_top_struct(Parser* p) {
    const Token struct_kw = P_next_token(p);
    if (!Token_cmp(struct_kw, "struct")) {
        fprintf(stderr, "[internal] should be 'struct' (from calling this fn)\n");
        exit(3);
    }

    P_require_having_next_token(p, TT_Identifier);
    const Token raw_struct_name = P_next_token(p);
    const Identifier struct_name = P_to_identifier(p, raw_struct_name);

    P_require_next_be(p, TT_StartCurlyBracket, SV_from_str_len("{", 1));

    IR_field_LIST fields = IR_field_LIST_new();


    while (1) {
        if (!P_has_next(p)) {
            expected_token_error(p->source, p->source_filename, &p->positioner.ref->array[p->positioner.ref->len - 1],
                                 "}");
            exit(1);
        }
        const Token nx = P_peek_token(p);
        if (Token_cmp(nx, "}")) {
            P_next_token(p);
            break;
        }
        const IR_typeuse ty = P_get_typeuse(p);
        P_require_having_next_token(p, TT_Identifier);
        const Token raw_field_name = P_next_token(p);
        const Identifier field_name = P_to_identifier(p, raw_field_name);

        IR_field_LIST_push(&fields, (IR_field){
                               .name = field_name,
                               .type = ty,
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

    // P_require_next_be(p, TT_EndCurlyBracket, SV_from_str_len("}", 1));

    IR_structdef_LIST_push(&p->type_table.structs, (IR_structdef){
                               .name = struct_name,
                               .fields = fields,
                           });

    // UNIMPLEMENTED("P_get_top_struct");
}

// void P_get_top_enum(Parser* p) { UNIMPLEMENTED("P_get_top_enum"); }
// void P_get_top_global(Parser* p) { UNIMPLEMENTED("P_get_top_global"); }

void P_get_top_fn(Parser* p) {
    p->temporal_fn_cnt = 0;
    p->compilation_scope = P_compilation_scope_LIST_new();

    const IR_typeuse fn_ret_t = P_get_typeuse(p);

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
        const IR_typeuse ty = P_get_typeuse(p);

        P_require_having_next_token(p, TT_Identifier);
        const Token pre_name_token = &p->positioner.ref->array[p->positioner.pos - 1];
        const Token name = P_next_token(p);
        if (Token_cmp(name, ",")) {
            expected_token_error(p->source, p->source_filename, pre_name_token, "argument name");
            exit(1);
        }

        const Identifier id_name = P_to_identifier(p, name);

        IR_typename_pair_LIST_push(&args, (IR_typename_pair){.type = ty, .name = id_name});

        if (Token_cmp(P_peek_token(p), ")")) {
            break;
        }
        P_require_next_be(p, TT_Comma, SV_from_str_len(",", 1));
    }


    P_require_next_be(p, TT_EndBracket, SV_from_str_len(")", 1));

    const IR_block_LIST blocks = P_get_isolated_function_code_section(p);

    IR_func_LIST_push(&p->functions, (IR_func){
                          .fn_ret_t = fn_ret_t,
                          .blocks = blocks,
                          .args = args,
                          .fn_name = fn_name
                      });
    printf("[info] parsed function '"SV_format"'\n", SV_p_args(raw_fn_name));


    // cleanup
    P_compilation_scope_LIST_free(&p->compilation_scope);
}

void P_get_top(Parser* p) {
    const Token t1 = P_peek_token(p);
    if (Token_cmp(t1, "struct")) {
        P_get_top_struct(p);
        // } else if (Token_cmp(t1, "enum")) {
        //     P_get_top_enum(p);
        // } else if (Token_cmp(t1, "global")) {
        //     P_get_top_global(p);
    } else {
        P_get_top_fn(p);
    }
}


void parse(SVList* restrict tokens, const SV* restrict source, const SV* restrict source_filename) {
    Parser p = (Parser){
        .positioner = SVList_to_view(tokens),
        .source = source,
        .source_filename = source_filename,
        .functions = IR_func_LIST_new(),
        .identifier_map = kh_init(sv_u64_map),
        .identifier_map_cnt = 1,
        .type_table = (TypeTable){
            .structs = IR_structdef_LIST_new()
        }
    };


    while (P_has_next(&p)) {
        P_get_top(&p);
    }


    // ---------------------------------------------------------------------------------

    printf("TYPES:\n");
    for (size_t k = 0; k < p.type_table.structs.len; k++) {
        IR_structdef sdef = p.type_table.structs.array[k];

        const SV struct_name = SV_from_identifiermap_lookup(p.identifier_map, sdef.name);
        printf("  STRUCT <"SV_format"> {\n", SV_v_args(struct_name));
        for (size_t j = 0; j < sdef.fields.len; j++) {
            const IR_field f = sdef.fields.array[j];
            const SV field_name = SV_from_identifiermap_lookup(p.identifier_map, f.name);

            printf("    FIELD <");
            print_IR_typeuse(&f.type, p.identifier_map);
            printf("> '"SV_format"'\n", SV_v_args(field_name));
        }
    }

    printf("FUNCTIONS:\n");
    for (size_t f = 0; f < p.functions.len; f++) {
        const Identifier fn_name = p.functions.array[f].fn_name;

        const SV real_fn_name = SV_from_identifiermap_lookup(p.identifier_map, fn_name);

        printf("FUNC <"SV_format"> {\n", SV_v_args(real_fn_name));

        for (size_t b = 0; b < p.functions.array[f].blocks.len; b++) {
            const IR_block* block = &p.functions.array[f].blocks.array[b];
            printf("  block %lu,\n", b);
            printf("    binds: { ");
            for (size_t n = 0; n < block->bindings.len; n++) {
                const IR_SSA_binding bind = block->bindings.array[n];
                const SV var = SV_from_identifiermap_lookup(p.identifier_map, bind.variable);
                printf("'"SV_format"' %%%lu", SV_v_args(var), bind.bind.val);
                if (n + 1 != block->bindings.len) {
                    printf(", ");
                }
            }
            printf(" }\n");


            for (size_t i = 0; i < block->instructions.len; i++) {
                printf("    ");
                print_IR_instr(&block->instructions.array[i], p.identifier_map);
                printf("\n");
            }
            printf("  }\n");
        }
        printf("}\n");
    }
}
