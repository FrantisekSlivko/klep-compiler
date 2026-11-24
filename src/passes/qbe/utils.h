#ifndef KLEPCOMPILER_QBE_UTILS_H
#define KLEPCOMPILER_QBE_UTILS_H
#include "ir_defs.h"
#include "target.h"
#include "typecompletion.h"

void qbe_fn_call_type(const TC_typeid* restrict t, const TargetInfo* restrict target_info, char* out);

typedef struct {
    bool is_float_primitive;
    bool is_signed_int_primitive;
    uint8_t primitive_byte_size;
} TC_primitive_classification;

TC_primitive_classification tc_primitive_classification(TC_Primitive_TY t);

char qbe_val_type(const TC_typeid* t, const TargetInfo* restrict target_info);


#endif //KLEPCOMPILER_QBE_UTILS_H
