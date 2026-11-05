
#ifndef KLEPCOMPILER_TYPECHECKING_TYPES_H
#define KLEPCOMPILER_TYPECHECKING_TYPES_H

#include "typecompletion.h"
#include <stddef.h>


typedef struct {
    TC_typeid type;
    SpannedIdentifier name;
} TC_arg;

init_prefixed_list_type(TC_arg)


typedef struct {
    size_t block;
    size_t instr_nth;
} TC_InstrPos;

init_prefixed_list_type(TC_InstrPos)

typedef enum {
    TC_VK_Int,
    TC_VK_Float,
    TC_VK_Bool,
    TC_VK_Array,
} TC_Value_kind;

typedef struct TC_Value_LIST TC_Value_LIST;

typedef struct TC_Value {
    TC_Value_kind kind;

    union {
        int64_t Int;
        float64_t Float;
        bool Bool;
        TC_Value_LIST* Array;
    };
} TC_Value;

init_prefixed_list_type(TC_Value)


typedef struct {
    /// false if not set
    bool known_temporal;

    TC_typeid type;

    /// will have parallel array with values
    bool is_value_known;
    // is set on the linear pass
    bool is_addressable;
    // union {
    //     void* null;
    //     TC_Value value;
    // };
} TC_Entry;

init_prefixed_list_type(TC_Entry)


typedef struct {
    IR_temporal temporal;
    TC_typeid type;
} IR_temporal_and_TC_type;

typedef struct {
    IR_temporal a;
    IR_temporal b;
} DoubleIR_temporal;

typedef struct {
    IR_temporal a;
    IR_temporal b;
    TC_typeid t;
} DoubleIR_temporal_and_TC_typeid;

typedef struct {
    bool has_rewrite;
    IR_temporal rewrite;
} IR_rewrite;

init_prefixed_list_type(IR_rewrite)


typedef struct {
    IR_temporal from;
    IR_temporal to;
} IR_temp_map;

init_prefixed_list_type(IR_temp_map)



#endif //KLEPCOMPILER_TYPECHECKING_TYPES_H
