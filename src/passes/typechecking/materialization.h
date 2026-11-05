
#ifndef KLEPCOMPILER_MATERIALIZATION_H
#define KLEPCOMPILER_MATERIALIZATION_H


#include "ir.h"
#include "typecompletion.h"

const IR_func* materialize_function(const IR_generic_function* restrict gf, const TC_typeid_LIST found_args_t,
                                    const TC_typeid* found_ret_t, TC_TypeTable* restrict nct);

#endif //KLEPCOMPILER_MATERIALIZATION_H
