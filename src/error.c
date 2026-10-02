#include "error.h"

#include <stdint.h>
#include <stdio.h>

#include "utils/color.h"

typedef struct {
    size_t line_start;
    size_t line_len;
    size_t line_num;
    size_t inline_start;
} TokenLocation;

void print_error(const SV* restrict filename, size_t line_num, size_t char_num, const SV* message, const SV* code_line, size_t ptr_start, size_t ptr_len) {
    fprintf(stderr, "\n");

    fprintf(stderr, ""SV_format":%lu:%lu: "C_RED"error:"C_RESET" "SV_format"\n", SV_p_args(filename), line_num, char_num, SV_p_args(message));

    fprintf(stderr, "  "SV_format"\n", SV_p_args(code_line));
    fprintf(stderr, "  ");
    for (size_t n = 0; n < ptr_start; n++ ) {
        putc(' ', stderr);
    }
    for (size_t n = 0; n < ptr_len; n++) {
        putc('^', stderr);
    }
    putc('\n', stderr);
}

void print_type_error(const SV* restrict filename, size_t line_num, size_t char_num, const SV* message, const SV* code_line, size_t ptr_start, size_t ptr_len) {
    fprintf(stderr, "\n");

    fprintf(stderr, ""SV_format":%lu:%lu: "C_RED"type error:"C_RESET" "SV_format"\n", SV_p_args(filename), line_num, char_num, SV_p_args(message));

    fprintf(stderr, "  "SV_format"\n", SV_p_args(code_line));
    fprintf(stderr, "  ");
    for (size_t n = 0; n < ptr_start; n++ ) {
        putc(' ', stderr);
    }
    for (size_t n = 0; n < ptr_len; n++) {
        putc('^', stderr);
    }
    putc('\n', stderr);
}

void print_hint(const SV* restrict filename, size_t line_num, size_t char_num, const SV* message, const SV* code_line, size_t ptr_start, size_t ptr_len) {
    fprintf(stderr, "\n");

    fprintf(stderr, ""SV_format":%lu:%lu: "C_CYAN"hint:"C_RESET" "SV_format"\n", SV_p_args(filename), line_num, char_num, SV_p_args(message));

    fprintf(stderr, "  "SV_format"\n", SV_p_args(code_line));
    fprintf(stderr, "  ");
    for (size_t n = 0; n < ptr_start; n++ ) {
        putc(' ', stderr);
    }
    for (size_t n = 0; n < ptr_len; n++) {
        putc('^', stderr);
    }
    putc('\n', stderr);
}

TokenLocation locate_token(const SV* source, const SV* token) {
    size_t line_num = 0;
    const char* r_line_start = source->start;
    size_t char_num = 0;

    size_t i = 0;
    for (; i < source->len; i++) {
        if (source->start[i] != '\n' && r_line_start == 0) {
            r_line_start = &source->start[i];
        }

        if (&source->start[i] == token->start) {
            break;
        }

        if (source->start[i] != '\n') {
            char_num++;
        } else {
            char_num = 0;
            line_num++;
            r_line_start = 0;
        }

    }

    size_t char_num_end = char_num;
    for (; i < source->len && source->start[i] != '\n'; i++) char_num_end++;

    return (TokenLocation) {
        .line_start = (r_line_start - source->start),
        .line_len =  char_num_end,
        .line_num = line_num,
        .inline_start = char_num
    };
}

void syntax_error(const SV* source, const SV* restrict source_filename, const SV* token, const SV* message) {
    const TokenLocation tl = locate_token(source, token);

    const SV line = SV_from_str_len(source->start + tl.line_start, tl.line_len);
    print_error(source_filename, tl.line_num+1, tl.inline_start+1, message, &line, tl.inline_start, token->len);
}

void type_error(const SV* source, const SV* restrict source_filename, const SV* token, const SV* message) {
    const TokenLocation tl = locate_token(source, token);

    const SV line = SV_from_str_len(source->start + tl.line_start, tl.line_len);
    print_type_error(source_filename, tl.line_num+1, tl.inline_start+1, message, &line, tl.inline_start, token->len);
}

void expected_token_error(const SV* source, const SV* restrict source_filename, const SV* last_token, const char* restrict expected) {

    const TokenLocation tl = locate_token(source, last_token);

    const SV line = SV_from_str_len(source->start + tl.line_start, tl.line_len);

    char* raw_message = calloc(1, 100);
    snprintf(raw_message, 100, "expected '%s'", expected);

    const SV message = SV_from_str(raw_message);

    print_error(source_filename, tl.line_num+1, tl.inline_start+last_token->len+1, &message, &line, tl.inline_start + last_token->len, 1);

    free(raw_message);
}

void hint_standalone(const SV* message) {
    fprintf(stderr, ""C_CYAN"hint:"C_RESET" "SV_format"\n", SV_p_args(message));
}

void hint_on_code(const SV* source, const SV* restrict source_filename, const SV* token, const SV* message) {
    const TokenLocation tl = locate_token(source, token);

    const SV line = SV_from_str_len(source->start + tl.line_start, tl.line_len);
    print_hint(source_filename, tl.line_num+1, tl.inline_start+1, message, &line, tl.inline_start, token->len);
}
