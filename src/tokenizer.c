#include "tokenizer.h"

#include <stdbool.h>


bool is_operator(const char c) {
    return c == '+' || c == '-' ||
           c == '*' || c == '/' ||
           c == '<' || c == '>' ||
           c == '=' || c == '%' ||
           c == '&' || c == '|' || c == '!';
}


static inline bool p_is_whitespace(const char c) {
    return c == ' ' || c == '\n' || c == '\t';
}

static inline bool p_is_single_char_token(const char c) {
    return is_operator(c) ||
        c == '(' || c == ')' ||
        c == '{' || c == '}' ||
        c == '[' || c == ']' ||
        c == '<' || c == '>' ||
        c == '=' ||
        c == ';' || c == ',' || c == ':' || c == '.';
}

static inline bool p_is_word_char(const char c) {
    return !p_is_whitespace(c) &&
           !p_is_single_char_token(c) &&
           c != '"';
}

static inline bool p_is_double_char_token(const char a, const char b) {
    return (a == '=' && b == '=') ||
           (a == '!' && b == '=') ||
           (a == '<' && b == '=') ||
           (a == '>' && b == '=') ||
           (a == '&' && b == '&') ||
           (a == '|' && b == '|') ||
           (a == '+' && b == '+') ||
           (a == '-' && b == '-') ||
           (a == '+' && b == '=') ||
           (a == '-' && b == '=') ||
           (a == '*' && b == '=') ||
           (a == '/' && b == '=');


           // (a == '<' && b == '<') ||
           // (a == '>' && b == '>');
}

static inline bool p_is_digit(const char c) {
    return c >= '0' && c <= '9';
}

/// tokenization function
SVList p_tokenize(const SV* sv) {
    SVList ot = SVList_new();
    size_t i = 0;
    while (i < sv->len) {
        const char c = sv->start[i];

        if (p_is_whitespace(c)) {
            i++;
            continue;
        }


        // comment
        if (c == '/' && i + 1 < sv->len && sv->start[i + 1] == '/') {
            while (i < sv->len && sv->start[i] != '\n') i++;
            continue;
        }

        if (p_is_single_char_token(c)) {
            if (i + 1 < sv->len && p_is_double_char_token(c, sv->start[i + 1])) {
                SVList_push(&ot, SV_lrslice_from_SV(sv, i, 2));
                i += 2;
            } else {
                SVList_push(&ot, SV_lrslice_from_SV(sv, i, 1));
                i++;
            }
            continue;
        }

        if (c == '"') {
            const size_t start = i++;
            while (i < sv->len && sv->start[i] != '"' &&
                   sv->start[i] != '\n' && sv->start[i] != '\0') {
                if (sv->start[i] == '\\') i++;
                i++;
            }
            i++;
            SVList_push(&ot, SV_lrslice_from_SV(sv, start, i - start));
            continue;
        }

        // new
        if (p_is_digit(c)) {
            const size_t start = i;
            while (i < sv->len && p_is_digit(sv->start[i])) i++;

            if (i < sv->len && sv->start[i] == '.' &&
                i + 1 < sv->len && p_is_digit(sv->start[i + 1])) {
                i++;
                while (i < sv->len && p_is_digit(sv->start[i])) i++;
                }

            SVList_push(&ot, SV_lrslice_from_SV(sv, start, i - start));
            continue;
        }
        // end new

        const size_t start = i;
        while (i < sv->len && p_is_word_char(sv->start[i])) i++;
        SVList_push(&ot, SV_lrslice_from_SV(sv, start, i - start));
    }
    return ot;
}