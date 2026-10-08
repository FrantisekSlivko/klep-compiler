#ifndef KLEPCOMPILER_MIR_H
#define KLEPCOMPILER_MIR_H
#include "typecompletion.h"
#include "list.h"

typedef struct {
    TC_typeid type;
    bool is_addressable; // True if it needs a stack slot (e.g., its address is taken)
    SizeAlign sz;
} MIR_temporal;

init_prefixed_list_type(MIR_temporal)

typedef uint64_t MIR_temporal_id;

typedef enum {
    MIR_TY__INVALID = 0,

} MIR_instr_ty;

typedef struct {
    MIR_instr_ty type;
    union {
        struct {
            int64_t val;
            uint8_t bytes_size;
            MIR_temporal_id dest;
        } const_int;
        struct {
            float64_t val;
            uint8_t bytes_size;
            MIR_temporal_id dest;
        } const_float;
    } info;
} MIR_instr;

init_prefixed_list_type(MIR_instr)

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

#endif //KLEPCOMPILER_MIR_H
