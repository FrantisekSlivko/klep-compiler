#ifndef KLEPCOMPILER_PARSER_H
#define KLEPCOMPILER_PARSER_H
#include "string_view.h"
#include <stdint.h>

#include "ir.h"
#include "khash.h"
#include "sv_u64_map.h"
#include "list.h"

#include "source_info.h"
#include "target.h"

SV SV_from_identifiermap_lookup(const khash_t(sv_u64_map)* restrict identifier_map, uint64_t identifier);

typedef enum {
    P_OP__ADD,
    P_OP__SUB,
    P_OP__DIV,
    P_OP__MUL,
    P_OP__POW,

    P_OP__CMP_EQ,
    P_OP__CMP_NOT_EQ,
    P_OP__CMP_LESS,
    P_OP__CMP_LESS_EQ,
    P_OP__CMP_GREATER,
    P_OP__CMP_GREATER_EQ,

    P_OP__OR,
    P_OP__AND,
} P_OP;

init_list_type(P_OP, P_OP_LIST)





/// bool
#define Token_cmp(t,val) SV_pv_cmp_eq((t), val, sizeof(val)-1)

typedef const SV* Token;

void parse(SVList* restrict tokens, SourceInfo source_info, TargetInfo target_info);

uint64_t* find_identifier_from_sv(const khash_t(sv_u64_map)* restrict identifier_map, Token restrict t);

#endif //KLEPCOMPILER_PARSER_H
