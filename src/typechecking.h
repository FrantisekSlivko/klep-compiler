#ifndef KLEPCOMPILER_TYPECHECKING_H
#define KLEPCOMPILER_TYPECHECKING_H
#include <stdint.h>
#include "ir_defs.h"

#include "common_parser_typecheck.h"

#include "ir.h"
#include "list.h"
#include "parser.h"
#include "target.h"
#include "source_info.h"

bool TC_Primitive_TY_from_u32(uint32_t num, TC_Primitive_TY* restrict out);
bool TC_Primitive_TY_from_SV(const SV* sv, TC_Primitive_TY* restrict out);
uint32_t TC_Primitive_TY_to_u32(TC_Primitive_TY pt);
SV TC_Primitive_TY_to_SV(TC_Primitive_TY pt);

uint32_t TC_Primitive_TY_size(TC_Primitive_TY pt);

typedef enum {
    TC_TY_Primitive,
    TC_TY_Struct,
    TC_TY_Enum,
    TC_TY_Ref,
    TC_TY_Array,
    TC_TY_List,
    TC_TY_Box,
    // TC_TY_Generic,
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
            Identifier generics_name;
        } generic;
    } info;
} TC_typeid;



typedef struct {
    Identifier name;
    SV raw_name;
    TC_typeid type; // IR_TY_Named for structs, IR_TY_Primitive for builtins, etc.
} TC_field;

init_list_type(TC_field, TC_field_LIST)


typedef struct {
    uint32_t size;
    uint32_t align;
    uint32_t* field_offsets; // length = fields.len of corresponding structdef
} TC_struct_layout;


typedef struct {
    Identifier name;
    SV raw_name;
    TC_field_LIST fields;
    TC_struct_layout layout;
} TC_structdef;

init_prefixed_list_type(TC_structdef)


typedef struct {
    Identifier name;
    SV raw_name;
    TC_typeid* values;
    /// =0 if without values
    size_t values_len;
} TC_enumdef_variant;
init_prefixed_list_type(TC_enumdef_variant)

typedef struct {
    Identifier name;
    SV raw_name;
    TC_enumdef_variant_LIST variants;
    uint32_t size;
    uint32_t discriminant_size;
    uint32_t align;
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


typedef struct TC_TypeTable {
    TC_structdef_LIST structs;
    // TC_struct_layout* struct_layouts;

    TC_enumdef_LIST enums;
    // size_t* enum_sizes;

    // TC_materialized_generic_structdef_LIST materialized_generic_structs;
    // TC_materialized_generic_enumdef_LIST materialized_generic_enums;
    TC_materialized_generic_index_record_LIST materialized_generics;
} TC_TypeTable;


TC_TypeTable close_type_table(
    const IR_incomplete_TypeTable* table,
    const khash_t(sv_u64_map)* identifier_map,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info
);

void print_TC_typeid(const TC_typeid* restrict ty, khash_t(sv_u64_map)* restrict identifier_map, const TC_TypeTable* restrict tt);

#endif //KLEPCOMPILER_TYPECHECKING_H
