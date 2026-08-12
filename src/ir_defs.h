#ifndef KLEPCOMPILER_IR_DEFS_H
#define KLEPCOMPILER_IR_DEFS_H
#include <stdint.h>
#include "list.h"
#include "string_view.h"

typedef uint64_t Identifier;


typedef enum {
    TC_Primitive__Int8 = 1,
    TC_Primitive__Int16 = 2,
    TC_Primitive__Int32 = 3,
    TC_Primitive__Int64 = 4,
    TC_Primitive__Uint8 = 5,
    TC_Primitive__Uint16 = 6,
    TC_Primitive__Uint32 = 7,
    TC_Primitive__Uint64 = 8,
    TC_Primitive__Float32 = 9,
    TC_Primitive__Float64 = 10,
    TC_Primitive__Char = 11,
} TC_Primitive_TY;


typedef enum {
    IR_TY_Primitive,
    IR_TY_Named,
    IR_TY_Ref,
    IR_TY_Array,
    IR_TY_List,
} IR_typeuse_type;

typedef struct IR_typeuse {
    union {
        TC_Primitive_TY primitive;
        Identifier name;
    } info;
    IR_typeuse_type tt_ty;
    struct IR_typeuse* contained;
} IR_typeuse;

typedef struct {
    Identifier name;
    IR_typeuse type;
} IR_field;

init_list_type(IR_field, IR_field_LIST)

typedef struct {
    Identifier name;
    SV raw_name;
    IR_field_LIST fields;
} IR_structdef;

init_list_type(IR_structdef, IR_structdef_LIST)

typedef struct {
    IR_structdef_LIST structs;
} IR_incomplete_TypeTable;

#endif //KLEPCOMPILER_IR_DEFS_H
