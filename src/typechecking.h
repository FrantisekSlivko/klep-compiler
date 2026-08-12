#ifndef KLEPCOMPILER_TYPECHECKING_H
#define KLEPCOMPILER_TYPECHECKING_H
#include <stdint.h>
#include "ir_defs.h"


#include "ir.h"
#include "list.h"
#include "parser.h"
#include "target.h"
#include "source_info.h"

bool TC_Primitive_TY_from_u32(uint32_t num, TC_Primitive_TY* restrict out);
bool TC_Primitive_TY_from_SV(const SV* sv, TC_Primitive_TY* restrict out);
uint32_t TC_Primitive_TY_to_u32(TC_Primitive_TY pt);
SV TC_Primitive_TY_to_SV(TC_Primitive_TY pt);

uint8_t TC_Primitive_TY_size(TC_Primitive_TY pt);

typedef enum {
    TC_TY_Primitive,
    TC_TY_Struct,
    TC_TY_Ref,
    TC_TY_Array,
    TC_TY_List,
} TC_type_ty;

typedef struct TC_typeid {
    TC_type_ty ty;
    union {
        TC_Primitive_TY primitive;
        uint32_t type_id;
        struct TC_typeid* contained;
    } info;
} TC_typeid;



typedef struct {
    Identifier name;
    TC_typeid type; // IR_TY_Named for structs, IR_TY_Primitive for builtins, etc.
} TC_field;

init_list_type(TC_field, TC_field_LIST)

typedef struct {
    Identifier name;
    SV raw_name;
    TC_field_LIST fields;
} TC_structdef;

init_list_type(TC_structdef, TC_structdef_LIST)


typedef struct {
    uint32_t size;
    uint32_t align;
    uint32_t* field_offsets; // length = fields.len of corresponding structdef
} TC_struct_layout;


typedef struct {
    TC_structdef_LIST structs;
    TC_struct_layout* struct_layouts;
} TC_TypeTable;


TC_TypeTable close_type_table(
    const IR_incomplete_TypeTable* table,
    const khash_t(sv_u64_map)* identifier_map,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info
);

void print_TC_typeid(const TC_typeid* restrict ty, khash_t(sv_u64_map)* restrict identifier_map, const TC_TypeTable* restrict tt);

#endif //KLEPCOMPILER_TYPECHECKING_H
