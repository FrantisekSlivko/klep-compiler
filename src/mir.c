#include "mir.h"

void mir_emit(const MIR_emitter* restrict mir_emitter, const MIR_instr instr) {
    MIR_instr_LIST_push(mir_emitter->current_block_instrs, instr);
}




void mir_emit_load(const MIR_emitter* restrict emitter,
    const MIR_temporal_id dest,
    const MIR_temporal_id slot
) {
    mir_emit(emitter, (MIR_instr) {
        .type = MIR_TY__LOAD,
        .info.load = {
            dest,
            slot
        }
    });
}

void mir_emit_read_ptr(const MIR_emitter* restrict emitter,
    const MIR_temporal_id dest,
    const MIR_temporal_id ptr,
    const uint64_t offset,
    const uint64_t read_size
) {
    mir_emit(emitter, (MIR_instr) {
        .type = MIR_TY__READ_PTR,
        .info.read_ptr = {
            dest,
            ptr,
            offset,
            read_size,
        }
    });
}
// void mir_emit_cast(const MIR_emitter* restrict emitter, ) {
//     mir_emit(emitter, (MIR_instr) {
//         .type = MIR_TY__CAST,
//         .info.cast = {
//
//         }
//     });
// }
// void mir_emit_cmp_not_eq(const MIR_emitter* restrict emitter, new_temporal, src, 0) {}
// void mir_emit_ref_of(const MIR_emitter* restrict emitter, new_temporal, src) {}
// void mir_emit_struct_init(const MIR_emitter* restrict emitter) {}