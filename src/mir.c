#include "mir.h"

void mir_emit(MIR_instr_LIST* restrict mir_instr_list, const MIR_instr instr) {
    MIR_instr_LIST_push(mir_instr_list, instr);
}