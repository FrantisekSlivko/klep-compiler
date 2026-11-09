#ifndef KLEPCOMPILER_TYPECOMPLETION_H
#define KLEPCOMPILER_TYPECOMPLETION_H

// #include "common_parser_typecompletion.h"

// #include <stdint.h>
// #include "ir_defs.h"

// #include "ir.h"
// #include "list.h"
#include "ir.h"
#include "khash.h"
#include "sv_u64_map.h"
#include "target.h"
#include "source_info.h"

init_list_type(size_t, size_t_LIST)

typedef struct { size_t size; size_t align; } SizeAlign;

bool TC_Primitive_TY_from_u32(uint32_t num, TC_Primitive_TY* restrict out);
bool TC_Primitive_TY_from_SV(const SV* sv, TC_Primitive_TY* restrict out);
uint32_t TC_Primitive_TY_to_u32(TC_Primitive_TY pt);
SV TC_Primitive_TY_to_SV(TC_Primitive_TY pt);

uint32_t TC_Primitive_TY_size(TC_Primitive_TY pt);

typedef enum {
    TC_TY_Void,
    TC_TY_Primitive,
    TC_TY_Struct,
    TC_TY_Enum,
    TC_TY_Ref,
    TC_TY_Array,
    TC_TY_List,
    TC_TY_Box,
    TC_TY_Slice,
    /// represents unknown type (has only name)
    TC_TY_External_Type,
} TC_type_ty;

typedef struct TC_typeid {
    TC_type_ty ty;
    union {
        TC_Primitive_TY primitive;
        /// used by structs and enums
        uint32_t type_id;
        struct TC_typeid* contained;
        struct {
            struct TC_typeid* contained;
            size_t contained_len;
            SpannedIdentifier generics_name;
        } generic;
        struct {
            struct TC_typeid* contained;
            uint32_t array_size;
        } t_array;
        SpannedIdentifier external_type;
    } info;
    // TokenSpan full_typeuse_span;
} TC_typeid;

init_prefixed_list_type(TC_typeid)



typedef struct {
    SpannedIdentifier name;
    TC_typeid type; // IR_TY_Named for structs, IR_TY_Primitive for builtins, etc.
} TC_field;

init_list_type(TC_field, TC_field_LIST)


typedef struct {
    uint32_t size;
    uint32_t align;
    uint32_t* field_offsets; // length = fields.len of corresponding structdef
} TC_struct_layout;


typedef struct {
    SpannedIdentifier name;
    TC_field_LIST fields;
    TC_struct_layout layout;
    bool from_generic;
    /// index into TC_TypeTable. TC_materialized_generic_index_record_LIST materialized_generics
    size_t generic_cache_index;
} TC_structdef;

init_prefixed_list_type(TC_structdef)


typedef struct {
    SpannedIdentifier name;
    TC_typeid* values;
    /// =0 if without values
    size_t values_len;
} TC_enumdef_variant;
init_prefixed_list_type(TC_enumdef_variant)

typedef struct {
    SpannedIdentifier name;
    TC_enumdef_variant_LIST variants;
    uint32_t size;
    uint32_t discriminant_size;
    uint32_t align;
    bool from_generic;
    /// index into TC_TypeTable. TC_materialized_generic_index_record_LIST materialized_generics
    size_t generic_cache_index;
} TC_enumdef;

init_prefixed_list_type(TC_enumdef)


// typedef struct {
//     TC_typeid source_type;
//     TC_structdef materialized_struct;
// } TC_materialized_generic_structdef;
//
// init_prefixed_list_type(TC_materialized_generic_structdef)
//
//
// typedef struct {
//     TC_typeid source_type;
//     TC_enumdef materialized_enum;
// } TC_materialized_generic_enumdef;
//
// init_prefixed_list_type(TC_materialized_generic_enumdef)


typedef enum {
    TC_NIRK_Enum,
    TC_NIRK_Struct,
} TC_index_record_kind;

typedef struct {
    Identifier name;
    TC_index_record_kind kind;
    uint32_t nth;
} TC_name_index_record;
init_prefixed_list_type(TC_name_index_record)


typedef struct {
    IR_typeuse generic;
    TC_index_record_kind kind;
    uint32_t nth;
} TC_materialized_generic_index_record;
init_prefixed_list_type(TC_materialized_generic_index_record)


typedef struct {
    SpannedIdentifier generic_name;
    TC_typeid generic_name_t;
} TC_GenericArgMap;

init_prefixed_list_type(TC_GenericArgMap)

typedef struct {
    Identifier fn_name;
    IR_func fn;
    TC_GenericArgMap_LIST map;
} IR_materialized_generic_function;

init_prefixed_list_type(IR_materialized_generic_function)

typedef struct TC_TypeTable {
    TC_structdef_LIST structs;
    // TC_struct_layout* struct_layouts;

    TC_enumdef_LIST enums;
    // size_t* enum_sizes;

    TC_name_index_record_LIST name_index;


    // TC_materialized_generic_structdef_LIST materialized_generic_structs;
    // TC_materialized_generic_enumdef_LIST materialized_generic_enums;
    TC_materialized_generic_index_record_LIST materialized_generics;

    IR_generic_structdef_LIST generic_structs;
    IR_generic_enumdef_LIST generic_enums;

    IR_generic_function_LIST generic_functions;
    IR_materialized_generic_function_LIST materialized_generic_functions;

    SpannedIdentifier_LIST external_types;
} TC_TypeTable;


TC_TypeTable close_type_table(
    const IR_incomplete_TypeTable* table,
    const khash_t(sv_u64_map)* identifier_map,
    IR_generic_function_LIST generic_functions,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info
);

void print_TC_typeid(const TC_typeid* restrict ty, const khash_t(sv_u64_map)* restrict identifier_map, const TC_TypeTable* restrict nct);
SV format_TC_typeid(const TC_typeid* restrict ty, const khash_t(sv_u64_map)* restrict identifier_map, const TC_TypeTable* restrict nct);

bool CMP_IR_typeuse(const IR_typeuse* a, const IR_typeuse* b);

IR_typeuse substitute_typeuse(
    const IR_typeuse* restrict input,
    const SpannedIdentifier_LIST* restrict generic_args,
    const IR_typeuse* restrict args
);

typedef enum {
    TC_LAYOUT_DEP_STRUCT_FIELD,
    TC_LAYOUT_DEP_ENUM_VARIANT,
    TC_LAYOUT_DEP_ARRAY_TYPE,
} TC_layout_dependency_kind;

typedef struct {
    const TC_typeid* origin_type;
    TC_layout_dependency_kind dependency_kind; // can be infered from if enum or struct
    union {
        struct {
            SpannedIdentifier field_name;
            TC_typeid* field_type;
        } struct_field;
        struct {
            SpannedIdentifier variant_name;
            TC_typeid* value_type;
        } enum_variant;
        struct {
            void* null;
        } array_type;
    } dependency_info;
} TC_layout_dependency;

init_prefixed_list_type(TC_layout_dependency)


SizeAlign calc_type_size(
    const TC_typeid* restrict type,
    // const TC_structdef_LIST* restrict out_structs,
    // const TC_enumdef_LIST* restrict out_enums,
    const TC_TypeTable* restrict nct,

    TC_layout_dependency_LIST* restrict in_progress,

    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info,
    const khash_t(sv_u64_map)* restrict identifier_map
);


#endif //KLEPCOMPILER_TYPECOMPLETION_H
