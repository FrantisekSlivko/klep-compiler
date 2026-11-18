#ifndef KLEPCOMPILER_MIR_H
#define KLEPCOMPILER_MIR_H
#include "typecompletion.h"
#include "list.h"
#include "passes/inference_check_types.h"

typedef struct {
    TC_typeid type;
    bool is_addressable; // True if it needs a stack slot (e.g., its address is taken)
    SizeAlign sz;
} MIR_temporal;

init_prefixed_list_type(MIR_temporal)

typedef uint64_t MIR_temporal_id;
init_prefixed_list_type(MIR_temporal_id)

typedef enum {
    MIR_TY__INVALID = 0,
    MIR_TY__ALLOC,
    MIR_TY__CONST_INT,
    MIR_TY__CONST_FLOAT,
    MIR_TY__CONST_STRING,
    MIR_TY__MOVE,
    MIR_TY__LOAD,
    MIR_TY__STORE,
    MIR_TY__READ_PTR,
    MIR_TY__REF_OF,
    MIR_TY__CALC_PTR,
    MIR_TY__STRUCT_INIT,
    MIR_TY__MAKE_ARRAY,
    MIR_TY__MAKE_ENUM,
    MIR_TY__MAKE_BOX,
    MIR_TY__CAST,
    MIR_TY__OP_BINOP,
    MIR_TY__OP_CMP,
    MIR_TY__BRANCH,
    MIR_TY__JUMP,
    MIR_TY__CALL,
    MIR_TY__INDIRECT_CALL,
    MIR_TY__RETURN,
    MIR_TY__RETURN_VOID,
    MIR_TY__DROP,
    MIR_TY__PHI,
} MIR_instr_ty;


typedef struct {
    uint32_t offset;
    MIR_temporal_id temp;
} MIR_struct_init_field;
init_prefixed_list_type(MIR_struct_init_field)

typedef struct {
    uint64_t block_id;
    MIR_temporal_id temp;
} MIR_phi_arg;
init_prefixed_list_type(MIR_phi_arg)

typedef enum {
    MIR_CMP_EQ = 0,
    MIR_CMP_NOT_EQ,
    MIR_CMP_LESS,
    MIR_CMP_LESS_EQ,
    MIR_CMP_GREATER,
    MIR_CMP_GREATER_EQ,
} MIR_cmp_kind;

typedef enum {
    MIR_BINOP_ADD = 0,
    MIR_BINOP_SUB,
    MIR_BINOP_MUL,
    MIR_BINOP_DIV,
    MIR_BINOP_MOD,
    MIR_BINOP_POW,
    MIR_BINOP_BIT_AND,
    MIR_BINOP_BIT_OR,
    MIR_BINOP_BIT_XOR,
    MIR_BINOP_SHL,
    MIR_BINOP_SHR,
} MIR_binop_kind;

typedef enum {
    MIR_CAST_EXPLICIT = 0,
    MIR_CAST_IMPLICIT,
} MIR_cast_kind;


typedef struct {
    MIR_instr_ty type;
    union {
        struct {
            MIR_temporal_id dest;
            int64_t val;
            TC_typeid type;
        } const_int;
        struct {
            MIR_temporal_id dest;
            float64_t val;
            TC_typeid type;
        } const_float;
        struct {
            MIR_temporal_id dest;
            SV str_val;
            TC_typeid type;
        } const_string;
        struct {
            MIR_temporal_id dest;
            MIR_temporal_id src;
        } move;
        struct {
            MIR_temporal_id dest;
            MIR_temporal_id slot;
        } load;
        struct {
            MIR_temporal_id ptr;
            MIR_temporal_id val;
        } store;
        struct {
            MIR_temporal_id dest;
            MIR_temporal_id ptr;
            uint64_t offset;
            uint64_t read_size;
        } read_ptr;
        struct {
            MIR_temporal_id dest;
            MIR_temporal_id src;
        } ref_of;
        struct {
            MIR_temporal_id dest;
            MIR_temporal_id base;
            uint64_t offset;
            uint64_t scale;
            MIR_temporal_id index;
            bool has_index;
        } calc_ptr;
        struct {
            MIR_temporal_id dest;
            /// index into nct.structs
            uint32_t struct_type_id;
            MIR_struct_init_field_LIST fields;
            SizeAlign sa;
        } struct_init;
        struct {
            MIR_temporal_id dest;
            TC_typeid array_type;
            TC_typeid item_type;
            MIR_temporal_id_LIST items;
            bool is_heap;
        } make_array;
        struct {
            MIR_temporal_id dest;
            /// index into nct.enums
            uint32_t enum_type_id;
            uint64_t variant_idx;
            MIR_temporal_id_LIST args;
            SizeAlign sa;
        } make_enum;
        struct {
            MIR_temporal_id dest;
            MIR_temporal_id src;
            SizeAlign inner_sa;
        } make_box;
        struct {
            MIR_temporal_id dest;
            MIR_temporal_id src;
            TC_typeid from_type;
            TC_typeid to_type;
            MIR_cast_kind cast_kind;
        } cast;
        struct {
            MIR_temporal_id dest;
            MIR_temporal_id a;
            MIR_temporal_id b;
            TC_typeid type;
            MIR_binop_kind op;
        } binop;
        struct {
            MIR_temporal_id dest;
            MIR_temporal_id a;
            MIR_temporal_id b;
            TC_typeid type;
            MIR_cmp_kind cmp_kind;
        } cmp;
        struct {
            MIR_temporal_id cond;
            uint64_t branch_true;
            uint64_t branch_false;
        } branch;
        struct {
            uint64_t target_block;
        } jump;
        struct {
            MIR_temporal_id dest;
            bool has_dest;
            SpannedIdentifier fn_name;
            SV raw_name;
            MIR_temporal_id_LIST args;
            TC_typeid_LIST arg_types;
            TC_typeid ret_type;
        } call;
        struct {
            MIR_temporal_id dest;
            bool has_dest;
            MIR_temporal_id fn_ptr;
            MIR_temporal_id_LIST args;
            TC_typeid ret_type;
        } indirect_call;
        struct {
            MIR_temporal_id val;
            TC_typeid type;
        } return_val;
        struct {
            MIR_temporal_id dest;
            TC_typeid type;
            bool drop_inside;
        } drop;
        struct {
            MIR_temporal_id dest;
            TC_typeid type;
            MIR_phi_arg_LIST args;
        } phi;
        struct {
            MIR_temporal_id dest;
            TC_typeid type;
            bool is_stack;
        } alloc;
    } info;
} MIR_instr;

init_prefixed_list_type(MIR_instr)

// typedef struct {
//     MIR_instr_LIST* current_block_instrs;
//     MIR_temporal_LIST* temporals;
//     TC_Entry_LIST* temporals_map;
//     size_t* highest_temporal;
//     const TargetInfo* target_info;
//     const TC_TypeTable* nct;
// } MIR_emitter;


typedef struct {
    MIR_instr_LIST instructions;
} MIR_block;

init_prefixed_list_type(MIR_block)

typedef struct {
    SpannedIdentifier fn_name;
    MIR_block_LIST blocks;
    size_t highest_temporal_number;
    MIR_temporal_LIST temporals;
} MIR_func;

// ------------------------------------

void mir_emit(
    MIR_instr_LIST* restrict mir_instr_list,
    MIR_instr instr
);




#endif //KLEPCOMPILER_MIR_H
