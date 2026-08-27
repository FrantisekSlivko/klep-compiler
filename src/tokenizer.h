#ifndef KLEPCOMPILER_TOKENIZER_H
#define KLEPCOMPILER_TOKENIZER_H
#include <stdbool.h>

#include "string_view.h"

bool is_operator(char c);
SVList p_tokenize(SV text);


#endif //KLEPCOMPILER_TOKENIZER_H
