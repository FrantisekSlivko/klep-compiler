#ifndef KLEPCOMPILER_ERROR_H
#define KLEPCOMPILER_ERROR_H
#include "string_view.h"

void syntax_error(const SV* source, const SV* restrict source_filename, const SV* token, const SV* message);

void expected_token_error(const SV* source, const SV* restrict source_filename, const SV* last_token, const char* expected);

#endif //KLEPCOMPILER_ERROR_H
