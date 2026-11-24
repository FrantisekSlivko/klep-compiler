#ifndef KLEPCOMPILER_BACKEND_H
#define KLEPCOMPILER_BACKEND_H
#include <stdio.h>

#include "args.h"
#include "string_view.h"

FILE* backend_open(const CompilerArgs* restrict args);
void backend_close(FILE* file, const CompilerArgs* restrict args);


#endif //KLEPCOMPILER_BACKEND_H
