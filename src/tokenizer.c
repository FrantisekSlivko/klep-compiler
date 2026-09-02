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
           (a == '/' && b == '=') ||
           (a == ':' && b == ':');


           // (a == '<' && b == '<') ||
           // (a == '>' && b == '>');
}

static inline bool p_is_digit(const char c) {
    return c >= '0' && c <= '9';
}

/// tokenization function
SVList p_tokenize(const SV text) {
    SVList ot = SVList_new();
    size_t i = 0;
    while (i < text.len) {
        const char c = text.start[i];

        if (p_is_whitespace(c)) {
            i++;
            continue;
        }


        // comment
        if (c == '/' && i + 1 < text.len && text.start[i + 1] == '/') {
            while (i < text.len && text.start[i] != '\n') i++;
            continue;
        }

        if (p_is_single_char_token(c)) {
            if (i + 1 < text.len && p_is_double_char_token(c, text.start[i + 1])) {
                SVList_push(&ot, SV_lrslice_from_SV(&text, i, 2));
                i += 2;
            } else {
                SVList_push(&ot, SV_lrslice_from_SV(&text, i, 1));
                i++;
            }
            continue;
        }

        if (c == '"') {
            const size_t start = i++;
            while (i < text.len && text.start[i] != '"' &&
                   text.start[i] != '\n' && text.start[i] != '\0') {
                if (text.start[i] == '\\') i++;
                i++;
            }
            i++;
            SVList_push(&ot, SV_lrslice_from_SV(&text, start, i - start));
            continue;
        }

        // new
        if (p_is_digit(c)) {
            const size_t start = i;
            while (i < text.len && p_is_digit(text.start[i])) i++;

            if (i < text.len && text.start[i] == '.' &&
                i + 1 < text.len && p_is_digit(text.start[i + 1])) {
                i++;
                while (i < text.len && p_is_digit(text.start[i])) i++;
                }

            SVList_push(&ot, SV_lrslice_from_SV(&text, start, i - start));
            continue;
        }
        // end new

        const size_t start = i;
        while (i < text.len && p_is_word_char(text.start[i])) i++;
        SVList_push(&ot, SV_lrslice_from_SV(&text, start, i - start));
    }
    return ot;
}