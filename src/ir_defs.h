#ifndef KLEPCOMPILER_IR_DEFS_H
#define KLEPCOMPILER_IR_DEFS_H
#include <stdint.h>
#include <stdio.h>

#include "list.h"
#include "string_view.h"
#include "utils/dev.h"


typedef uint64_t Identifier;
init_prefixed_list_type(Identifier)

typedef struct {
    SV sv;
    // todo: add file_id
} TokenSpan;
init_prefixed_list_type(TokenSpan)


static inline TokenSpan TS_merge(const TokenSpan* start, const TokenSpan* end) {
    if ((uintptr_t)(end->sv.start) < (uintptr_t)(start->sv.start)) {
        return TS_merge(end, start);
    }
    return (TokenSpan){
        (SV) {
            .start = start->sv.start,
            .len = (size_t)(end->sv.start - start->sv.start) + end->sv.len
        }
    };
}

typedef struct {
    Identifier identifier;
    TokenSpan span;
} SpannedIdentifier;

init_prefixed_list_type(SpannedIdentifier)

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
    TC_Primitive__Bool = 12,

    // these two can never be specified as, only appear from CONST_INT/CONST_FLOAT
    // TC_Primitive__Number = 13,
    // TC_Primitive__FloatNumber = 14,
} TC_Primitive_TY;


typedef enum {
    IR_TY_Void,
    IR_TY_Primitive,
    IR_TY_Named,
    IR_TY_Ref,
    IR_TY_Box,
    IR_TY_Slice,
    IR_TY_Array,
    IR_TY_List,
    IR_TY_Generic,
} IR_typeuse_type;

typedef struct IR_typeuse {
    union {
        TC_Primitive_TY primitive;
        SpannedIdentifier named;
    } info;
    IR_typeuse_type tt_ty;
    struct IR_typeuse* contained;
    union {
        size_t contained_len;
        /// used for IR_TY_Array
        uint32_t array_const_size;
    };
} IR_typeuse;

init_prefixed_list_type(IR_typeuse)

typedef struct {
    SpannedIdentifier name;
    IR_typeuse type;
} IR_field;

init_prefixed_list_type(IR_field)

typedef struct {
    SpannedIdentifier name;
    IR_field_LIST fields;
} IR_structdef;

init_prefixed_list_type(IR_structdef)


typedef struct {
    SpannedIdentifier name;
    IR_typeuse* values;
    /// =0 if without values
    size_t values_len;
} IR_enumdef_variant;

init_prefixed_list_type(IR_enumdef_variant)

typedef struct {
    SpannedIdentifier name;
    IR_enumdef_variant_LIST variants;
} IR_enumdef;

init_prefixed_list_type(IR_enumdef)

typedef struct {
    SpannedIdentifier name;
    SpannedIdentifier_LIST generic_args;
    IR_field_LIST fields;
} IR_generic_structdef;

init_prefixed_list_type(IR_generic_structdef)

typedef struct {
    SpannedIdentifier name;
    SpannedIdentifier_LIST generic_args;
    IR_enumdef_variant_LIST variants;
} IR_generic_enumdef;

init_prefixed_list_type(IR_generic_enumdef)







typedef enum {
    IR_IMPORT_KIND__Module,
    IR_IMPORT_KIND__ModuleAlias,
    IR_IMPORT_KIND__Selective,
    IR_IMPORT_KIND__Wildcard
} IR_import_kind;

typedef struct {
    IR_import_kind kind;
    SpannedIdentifier_LIST module_path;
    SpannedIdentifier alias;
    SpannedIdentifier_LIST items;
} IR_import;

init_prefixed_list_type(IR_import)







typedef enum CFFI_ty_kind {
    CFFI_TY_PRIMITIVE,
    CFFI_TY_PTR, // uses inner (for Int32* and Int32[])
    CFFI_TY_VOID,
    CFFI_TY_NAMED,
    CFFI_TY_ARRAY, // for Int32[5]
} CFFI_ty_kind;

typedef struct CFFI_c_typeuse {
    CFFI_ty_kind kind;
    union {
        struct CFFI_c_typeuse* inner;
        TC_Primitive_TY primitive;
        SpannedIdentifier named;
    };
    uint64_t array_len; // used for T[N]
} CFFI_c_typeuse;


typedef struct {
    CFFI_c_typeuse c_type;
    SpannedIdentifier arg_name;
} CFFI_c_fn_arg;

init_prefixed_list_type(CFFI_c_fn_arg)

typedef struct {
    IR_typeuse type;
    SpannedIdentifier name;
    bool has_constructor;
    SpannedIdentifier_LIST constructor;
    // Identifier_LIST constructor;
} CFFI_arg;

init_prefixed_list_type(CFFI_arg)

typedef struct {
    CFFI_c_typeuse c_ret_type;

    SpannedIdentifier c_fn_name;
    SpannedIdentifier kelp_fn_name;

    CFFI_c_fn_arg_LIST c_args;

    CFFI_arg ret;
    CFFI_arg_LIST args;
} CFFI_IR_extern_fn;

init_prefixed_list_type(CFFI_IR_extern_fn)






typedef struct {
    IR_structdef_LIST structs;
    IR_enumdef_LIST enums;

    IR_generic_structdef_LIST generic_structs;
    IR_generic_enumdef_LIST generic_enums;
    SpannedIdentifier_LIST extern_types;
    IR_import_LIST imports;
} IR_incomplete_TypeTable;

#endif //KLEPCOMPILER_IR_DEFS_H
