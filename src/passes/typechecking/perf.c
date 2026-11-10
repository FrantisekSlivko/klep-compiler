#include "perf.h"

#include "error.h"
#include "utils/color.h"
#include "passes/type_utils.h"


/// from value to stack slot
static bool is_promoted(const IR_temporal_LIST* promoted, IR_temporal t) {
    for (size_t n = 0; n < promoted->len; n++) {
        if (promoted->array[n].val == t.val) return true;
    }
    return false;
}



static bool is_scalar_loadable(const TC_typeid* t, const TC_TypeTable* nct, const TargetInfo* ti) {
    switch (t->ty) {
        case TC_TY_Primitive:
        case TC_TY_Ref:
        case TC_TY_Box:
            return calc_size_of(t, nct, ti).size <= ti->reg_size;
        default:
            return false;
    }
}

// todo: any use of value in slot needs LOAD on use as value
IR_temporal use_value(
    const IR_temporal t,
    size_t* restrict highest_temporal,
    TC_Entry_LIST* restrict temporals_map,
    const IR_temporal_LIST* promoted,
    const TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info
) {

    if (!is_promoted(promoted, t)) return t;
    if (!temporals_map->array[t.val].is_addressable) return t;
    if (!is_scalar_loadable(&temporals_map->array[t.val].type, nct, target_info)) return t;



    const uint64_t new_temporal = (*highest_temporal)++;
    printf("   %%%lu = LOAD %%%lu (from slot)\n", new_temporal, t.val);

    while(temporals_map->len <= new_temporal) TC_Entry_LIST_push(temporals_map, (TC_Entry) {
        .known_temporal = false
    });
    temporals_map->array[new_temporal] = temporals_map->array[t.val];
    temporals_map->array[new_temporal].is_addressable = false;

    return (IR_temporal){new_temporal};
}



// while Ref<T>, unpack
IR_temporal_and_TC_type perf_auto_deref(
    const TC_typeid* type,
    const IR_temporal* intake,
    size_t* highest_temporal,
    TC_Entry_LIST* temporals_map,
    const IR_temporal_LIST* promoted,
    const TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info
) {
    if (type->ty != TC_TY_Ref) {
        const IR_temporal v = use_value(*intake, highest_temporal, temporals_map, promoted, nct, target_info);
        return (IR_temporal_and_TC_type){.temporal = v, .type = *type}; //  = *intake
    }
    TC_typeid t = *type;
//     uint64_t te = intake->val;
    uint64_t te = use_value(*intake, highest_temporal, temporals_map, promoted, nct, target_info).val;
    while (t.ty == TC_TY_Ref) {
        const uint64_t new_temporal = (*highest_temporal)++;
        printf("   %%%lu = READ_PTR %%%lu\n", new_temporal, te);
        te = new_temporal;
        t = *t.info.contained;
        if (new_temporal + 1 > temporals_map->cap) {
            TC_Entry_LIST_inflate(temporals_map, new_temporal + 5);
        }
        temporals_map->array[new_temporal] = (TC_Entry){
            .type = t,
            .known_temporal = true,
            .is_value_known = false,
            .is_addressable = false,
        };
    }
    return (IR_temporal_and_TC_type){.temporal = (IR_temporal){.val = te}, .type = t};
}



/// returns numeric type that fits both
TC_typeid get_common_numeric_type(const TC_typeid t1, const TC_typeid t2) {
    if (t1.ty != TC_TY_Primitive) {
        INTERNAL_ERROR("should be primitive at this point")
    }
    if (t2.ty != TC_TY_Primitive) {
        INTERNAL_ERROR("should be primitive at this point")
    }

    /*
    RULE: Uint<X> to Uint<Y>, Int<X> to Int<Y>, Float<X> to Float<Y>, Uint<X> | Int<X> to Char/Bool
     */

    const TC_Primitive_TY p1 = t1.info.primitive;
    const TC_Primitive_TY p2 = t2.info.primitive;

    if (p1 == p2) return t1;

    if (p1 == TC_Primitive__Float32 && p2 == TC_Primitive__Float64) return t2;
    if (p1 == TC_Primitive__Float64 && p2 == TC_Primitive__Float32) return t1;

    switch (p1) {
        case TC_Primitive__Uint64: {
            switch (p2) {
                case TC_Primitive__Int8:
                case TC_Primitive__Int16:
                case TC_Primitive__Int32:
                case TC_Primitive__Int64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Uint8:
                case TC_Primitive__Uint16:
                case TC_Primitive__Uint32: {
                    return t1;
                }
                case TC_Primitive__Uint64: {
                    INTERNAL_ERROR();
                    break;
                }
                case TC_Primitive__Float32:
                case TC_Primitive__Float64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Char:
                case TC_Primitive__Bool: {
                    return t1;
                }
            }
            INTERNAL_ERROR("control flow")
        }
        case TC_Primitive__Uint32: {
            switch (p2) {
                case TC_Primitive__Int8:
                case TC_Primitive__Int16:
                case TC_Primitive__Int32:
                case TC_Primitive__Int64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Uint8:
                case TC_Primitive__Uint16: {
                    return t1;
                }
                case TC_Primitive__Uint32: {
                    INTERNAL_ERROR()
                    break;
                }
                case TC_Primitive__Uint64: {
                    return t2;
                }
                case TC_Primitive__Float32:
                case TC_Primitive__Float64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Char:
                case TC_Primitive__Bool: {
                    return t1;
                }
            }
            INTERNAL_ERROR("control flow")
        }
        case TC_Primitive__Uint16: {
            switch (p2) {
                case TC_Primitive__Int8:
                case TC_Primitive__Int16:
                case TC_Primitive__Int32:
                case TC_Primitive__Int64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Uint8: {
                    return t1;
                }
                case TC_Primitive__Uint16: {
                    INTERNAL_ERROR()
                    break;
                }
                case TC_Primitive__Uint32:
                case TC_Primitive__Uint64: {
                    return t2;
                }
                case TC_Primitive__Float32:
                case TC_Primitive__Float64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Char:
                case TC_Primitive__Bool: {
                    return t1;
                }
            }
            INTERNAL_ERROR("control flow")
        }
        case TC_Primitive__Uint8: {
            switch (p2) {
                case TC_Primitive__Int8:
                case TC_Primitive__Int16:
                case TC_Primitive__Int32:
                case TC_Primitive__Int64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Uint8: {
                    INTERNAL_ERROR()
                    break;
                }
                case TC_Primitive__Uint16:
                case TC_Primitive__Uint32:
                case TC_Primitive__Uint64: {
                    return t2;
                }
                case TC_Primitive__Float32:
                case TC_Primitive__Float64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Char:
                case TC_Primitive__Bool: {
                    return t1;
                }
            }
            INTERNAL_ERROR("control flow")
        }
        case TC_Primitive__Int64: {
            switch (p2) {
                case TC_Primitive__Int8:
                case TC_Primitive__Int16:
                case TC_Primitive__Int32: {
                    return t1;
                }
                case TC_Primitive__Int64: {
                    INTERNAL_ERROR()
                    break;
                }
                case TC_Primitive__Uint8:
                case TC_Primitive__Uint16:
                case TC_Primitive__Uint32:
                case TC_Primitive__Uint64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Float32:
                case TC_Primitive__Float64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Char:
                case TC_Primitive__Bool: {
                    return t1;
                }
            }
            INTERNAL_ERROR("control flow")
        }
        case TC_Primitive__Int32: {
            switch (p2) {
                case TC_Primitive__Int8:
                case TC_Primitive__Int16: {
                    return t1;
                }
                case TC_Primitive__Int32: {
                    INTERNAL_ERROR()
                    break;
                }
                case TC_Primitive__Int64: {
                    return t2;
                }
                case TC_Primitive__Uint8:
                case TC_Primitive__Uint16:
                case TC_Primitive__Uint32:
                case TC_Primitive__Uint64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Float32:
                case TC_Primitive__Float64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Char:
                case TC_Primitive__Bool: {
                    return t1;
                }
            }
            INTERNAL_ERROR("control flow")
        }
        case TC_Primitive__Int16: {
            switch (p2) {
                case TC_Primitive__Int8: {
                    return t1;
                }
                case TC_Primitive__Int16: {
                    INTERNAL_ERROR()
                    break;
                }
                case TC_Primitive__Int32:
                case TC_Primitive__Int64: {
                    return t2;
                }
                case TC_Primitive__Uint8:
                case TC_Primitive__Uint16:
                case TC_Primitive__Uint32:
                case TC_Primitive__Uint64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Float32:
                case TC_Primitive__Float64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Char:
                case TC_Primitive__Bool: {
                    return t1;
                }
            }
            INTERNAL_ERROR("control flow")
        }
        case TC_Primitive__Int8: {
            switch (p2) {
                case TC_Primitive__Int8: {
                    INTERNAL_ERROR()
                    break;
                }
                case TC_Primitive__Int16:
                case TC_Primitive__Int32:
                case TC_Primitive__Int64: {
                    return t2;
                }
                case TC_Primitive__Uint8:
                case TC_Primitive__Uint16:
                case TC_Primitive__Uint32:
                case TC_Primitive__Uint64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Float32:
                case TC_Primitive__Float64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Char:
                case TC_Primitive__Bool: {
                    return t1;
                }
            }
            INTERNAL_ERROR("control flow")
        }
        case TC_Primitive__Float32: {
            switch (p2) {
                case TC_Primitive__Int8:
                case TC_Primitive__Int16:
                case TC_Primitive__Int32:
                case TC_Primitive__Int64:
                case TC_Primitive__Uint8:
                case TC_Primitive__Uint16:
                case TC_Primitive__Uint32:
                case TC_Primitive__Uint64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Float32: {
                    INTERNAL_ERROR()
                    break;
                }
                case TC_Primitive__Float64: {
                    return t2;
                }
                case TC_Primitive__Char:
                case TC_Primitive__Bool: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
            }
            INTERNAL_ERROR("control flow")
        }
        case TC_Primitive__Float64: {
            switch (p2) {
                case TC_Primitive__Int8:
                case TC_Primitive__Int16:
                case TC_Primitive__Int32:
                case TC_Primitive__Int64:
                case TC_Primitive__Uint8:
                case TC_Primitive__Uint16:
                case TC_Primitive__Uint32:
                case TC_Primitive__Uint64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Float32: {
                    return t1;
                }
                case TC_Primitive__Float64: {
                    INTERNAL_ERROR()
                    break;
                }
                case TC_Primitive__Char:
                case TC_Primitive__Bool: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
            }
            INTERNAL_ERROR("control flow")
        }
        case TC_Primitive__Char:
        case TC_Primitive__Bool: {
            switch (p2) {
                case TC_Primitive__Int8:
                case TC_Primitive__Int16:
                case TC_Primitive__Int32:
                case TC_Primitive__Int64:
                case TC_Primitive__Uint8:
                case TC_Primitive__Uint16:
                case TC_Primitive__Uint32:
                case TC_Primitive__Uint64: {
                    return t2;
                }
                case TC_Primitive__Float32:
                case TC_Primitive__Float64: {
                    // INVALID
                    return (TC_typeid){.ty = TC_TY_Void};
                }
                case TC_Primitive__Char: {
                    return t2;
                }
                case TC_Primitive__Bool: {
                    return t1;
                }
            }
            INTERNAL_ERROR("control flow")
        }
    }

    // returns INVALID
    return (TC_typeid){.ty = TC_TY_Void};
}

/// returns TRUE when possible
/// describes intentional explicit casting by the user
bool can_be_explicit_casted(const TC_typeid* from, const TC_typeid* to) {
    if (from->ty == TC_TY_Primitive && to->ty == TC_TY_Primitive) {
        return true;
    }
    return false;
}


bool can_const_narrow(const uint64_t temporal, const TC_typeid* to, const TC_Entry_LIST* temporals_map,
                      const TC_Value_LIST* temporals_map_values) {
    if (temporals_map_values == NULL) return false;

    const TC_Entry* entry = &temporals_map->array[temporal];

    // If the compiler doesn't know the exact value at compile time, we must reject it.
    if (!entry->is_value_known) return false;
    if (to->ty != TC_TY_Primitive) return false;

    if (temporal + 1 > temporals_map_values->cap) {
        return false;
    }
    if (temporals_map_values->array[temporal].kind != TC_VK_Int) {
        return false;
    }

    const int64_t val = temporals_map_values->array[temporal].Int;

    switch (to->info.primitive) {
        case TC_Primitive__Int8: return val >= -128 && val <= 127;
        case TC_Primitive__Uint8: return val >= 0 && val <= 255;
        case TC_Primitive__Int16: return val >= -32768 && val <= 32767;
        case TC_Primitive__Uint16: return val >= 0 && val <= 65535;
        case TC_Primitive__Int32: return val >= -2147483648LL && val <= 2147483647LL;
        case TC_Primitive__Uint32: return val >= 0 && val <= 4294967295LL;
        case TC_Primitive__Char: return val >= 0 && val <= 255; // Or 1114111 for Unicode
        // Int64/Uint64 are already caught by implicit cast or are the max size anyway
        default: return false;
    }
    INTERNAL_ERROR("control flow error")
}

/// returns TRUE when possible
/// describes unintentional casting originating from two types not being equal (like Int32 a = 5_i16)
/// for unsigned to signed, allows only if fits, e.g Uint8 into Int16
bool can_be_implicit_casted(const TC_typeid* from, const TC_typeid* to) {
    if (to->ty == TC_TY_Primitive && from->ty == TC_TY_Primitive) {
        if (to->info.primitive == from->info.primitive) return true;

        switch (from->info.primitive) {
            case TC_Primitive__Int8: {
                return to->info.primitive >= TC_Primitive__Int8 && to->info.primitive <= TC_Primitive__Int64;
            }
            case TC_Primitive__Int16: {
                return to->info.primitive >= TC_Primitive__Int16 && to->info.primitive <= TC_Primitive__Int64;
            }
            case TC_Primitive__Int32: {
                return to->info.primitive >= TC_Primitive__Int32 && to->info.primitive <= TC_Primitive__Int64;
            }
            case TC_Primitive__Int64: {
                return to->info.primitive == TC_Primitive__Int64;
            }
            case TC_Primitive__Uint8: {
                const bool is_valid_unsigned = (to->info.primitive >= TC_Primitive__Uint8 && to->info.primitive <=
                                                TC_Primitive__Uint64);
                const bool is_safe_signed = (to->info.primitive >= TC_Primitive__Int16 && to->info.primitive <=
                                             TC_Primitive__Int64);
                return is_valid_unsigned || is_safe_signed;
            }
            case TC_Primitive__Uint16: {
                const bool is_valid_unsigned = (to->info.primitive >= TC_Primitive__Uint16 && to->info.primitive <=
                                                TC_Primitive__Uint64);
                const bool is_safe_signed = (to->info.primitive >= TC_Primitive__Int32 && to->info.primitive <=
                                             TC_Primitive__Int64);
                return is_valid_unsigned || is_safe_signed;
            }
            case TC_Primitive__Uint32: {
                const bool is_valid_unsigned = (to->info.primitive >= TC_Primitive__Uint32 && to->info.primitive <=
                                                TC_Primitive__Uint64);
                const bool is_safe_signed = (to->info.primitive == TC_Primitive__Int64);
                return is_valid_unsigned || is_safe_signed;
            }
            case TC_Primitive__Uint64: {
                return to->info.primitive == TC_Primitive__Uint64;
            }
            case TC_Primitive__Float32: {
                return to->info.primitive == TC_Primitive__Float32 || to->info.primitive == TC_Primitive__Float64;
            }
            case TC_Primitive__Float64: {
                return to->info.primitive == TC_Primitive__Float64;
            }
            case TC_Primitive__Char:
            case TC_Primitive__Bool: {
                return
                        (to->info.primitive >= TC_Primitive__Int8 && to->info.primitive <= TC_Primitive__Int64) ||
                        (to->info.primitive >= TC_Primitive__Uint8 && to->info.primitive <= TC_Primitive__Uint64);
            }
        }
    }
    return false;
}


/// on type ERROR: returns t->void, a->is_error -> T(1)/F(0), b->is_error -> T(1)/F(0)
DoubleIR_temporal_and_TC_typeid perf_numeric_promotion(
    const IR_temporal* a,
    const TC_typeid* a_type,
    const IR_temporal* b,
    const TC_typeid* b_type,
    size_t* highest_temporal,
    bool* is_out_error,
    bool* is_out_error_incompatible_number_relation,
    TC_Entry_LIST* temporals_map,
    const TC_Value_LIST* temporals_map_values,
    const IR_temporal_LIST* promoted,
    const TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const khash_t(sv_u64_map)* restrict identifier_map
) {
    const TC_typeid safe_a_type = *a_type;
    const TC_typeid safe_b_type = *b_type;


    const IR_temporal_and_TC_type derefered_a = perf_auto_deref(&safe_a_type, a, highest_temporal, temporals_map, promoted, nct, target_info);
    const IR_temporal_and_TC_type derefered_b = perf_auto_deref(&safe_b_type, b, highest_temporal, temporals_map, promoted, nct, target_info);

    {
        // check if derefered_a and derefered_b are number types
        bool is_error = false;
        uint64_t error_a = 0;
        uint64_t error_b = 0;

        if (derefered_a.type.ty != TC_TY_Primitive) {
            is_error = true;
            error_a = 1;
        } else {
            switch (derefered_a.type.info.primitive) {
                case TC_Primitive__Char:
                case TC_Primitive__Bool: {
                    is_error = true;
                    error_a = 1;
                    break;
                }
                default: {
                    break;
                }
            }
        }

        if (derefered_b.type.ty != TC_TY_Primitive) {
            is_error = true;
            error_b = 1;
        } else {
            switch (derefered_b.type.info.primitive) {
                case TC_Primitive__Char:
                case TC_Primitive__Bool: {
                    is_error = true;
                    error_b = 1;
                    break;
                }
                default: {
                    break;
                }
            }
        }

        if (is_error) {
            *is_out_error = true;
            *is_out_error_incompatible_number_relation = false;
            return (DoubleIR_temporal_and_TC_typeid){
                .a = (IR_temporal){.val = error_a},
                .b = (IR_temporal){.val = error_b},
                .t = (TC_typeid){.ty = TC_TY_Void}
            };
        }
    }

    TC_typeid target_op_type = {.ty = TC_TY_Void};

    // 1. CONSTANT PRIORITY RESOLUTION
    // Check if either side is a known compile-time value
    const bool a_known = temporals_map->array[derefered_a.temporal.val].is_value_known;
    const bool b_known = temporals_map->array[derefered_b.temporal.val].is_value_known;

    // If A is a variable and B is a constant that fits into A's type
    if (!a_known && b_known && can_const_narrow(derefered_b.temporal.val, &derefered_a.type, temporals_map,
                                                temporals_map_values)) {
        target_op_type = derefered_a.type;
    }
    // If B is a variable and A is a constant that fits into B's type
    else if (!b_known && a_known && can_const_narrow(derefered_a.temporal.val, &derefered_b.type, temporals_map,
                                                     temporals_map_values)) {
        target_op_type = derefered_b.type;
    }
    // 2. STANDARD PROMOTION
    else {
        target_op_type = get_common_numeric_type(derefered_a.type, derefered_b.type);

        // 3. FALLBACK: Mixed Signedness where get_common fails, but they are both constants
        if (target_op_type.ty == TC_TY_Void) {
            if (can_const_narrow(derefered_a.temporal.val, &derefered_b.type, temporals_map, temporals_map_values)) {
                target_op_type = derefered_b.type;
            } else if (can_const_narrow(derefered_b.temporal.val, &derefered_a.type, temporals_map,
                                        temporals_map_values)) {
                target_op_type = derefered_a.type;
            }
        }
    }

    if (target_op_type.ty == TC_TY_Void) {
        // incompatible integers -> between each other
        *is_out_error = true;
        *is_out_error_incompatible_number_relation = true;
        return (DoubleIR_temporal_and_TC_typeid){
            .a = (IR_temporal){.val = 0},
            .b = (IR_temporal){.val = 0},
            .t = target_op_type
        };
    }

    IR_temporal new_a = derefered_a.temporal;
    IR_temporal new_b = derefered_b.temporal;

    const SV t_op_sv = format_TC_typeid(&target_op_type, identifier_map, nct);

    // CAST A
    if (derefered_a.type.info.primitive != target_op_type.info.primitive) {
        const uint64_t new_temporal = (*highest_temporal)++;

        if (a_known) {

            printf("   %%%lu = CAST<"SV_format">(%%%lu) [IMPLICIT CONST-NARROW]\n", new_temporal,
                   SV_v_args(t_op_sv), new_a.val); // target_op_type.full_typeuse_span.sv
        } else {
            printf("   %%%lu = CAST<"SV_format">(%%%lu)\n", new_temporal,
                   SV_v_args(t_op_sv), new_a.val); // target_op_type.full_typeuse_span.sv
        }
        new_a.val = new_temporal;

        // IMPORTANT: Register the new temporal so the pipeline doesn't break later!
        if (temporals_map->cap < new_temporal + 1) TC_Entry_LIST_inflate(temporals_map, new_temporal + 5);
        temporals_map->array[new_temporal] = temporals_map->array[derefered_a.temporal.val];
        temporals_map->array[new_temporal].type = target_op_type;
    }

    // CAST B
    if (derefered_b.type.info.primitive != target_op_type.info.primitive) {
        const uint64_t new_temporal = (*highest_temporal)++;

        if (b_known) {
            printf("   %%%lu = CAST<"SV_format">(%%%lu) [IMPLICIT CONST-NARROW]\n", new_temporal,
                   SV_v_args(t_op_sv), new_b.val);
        } else {
            printf("   %%%lu = CAST<"SV_format">(%%%lu)\n", new_temporal,
                   SV_v_args(t_op_sv), new_b.val);
        }
        new_b.val = new_temporal;

        // IMPORTANT: Register the new temporal
        if (temporals_map->cap < new_temporal + 1) TC_Entry_LIST_inflate(temporals_map, new_temporal + 1);
        temporals_map->array[new_temporal] = temporals_map->array[derefered_b.temporal.val];
        temporals_map->array[new_temporal].type = target_op_type;
    }

    *is_out_error = false;
    *is_out_error_incompatible_number_relation = false;
    return (DoubleIR_temporal_and_TC_typeid){
        .a = new_a,
        .b = new_b,
        .t = target_op_type
    };
}

// The Condition Coercer
IR_temporal perf_bool(
    const IR_temporal* intake,
    const TC_typeid* intake_type,
    size_t* highest_temporal,
    TC_Entry_LIST* temporals_map,
    const IR_temporal_LIST* promoted,
    const TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info
) {
    const IR_temporal_and_TC_type derefd = perf_auto_deref(intake_type, intake, highest_temporal, temporals_map, promoted, nct, target_info);
    if (derefd.type.ty == TC_TY_Primitive && derefd.type.info.primitive == TC_Primitive__Bool) {
        return derefd.temporal;
    }
    if (derefd.type.ty == TC_TY_Primitive) {
        // emit check not zero
        // OP_CMP_NOT_EQ
        const uint64_t new_temporal = (*highest_temporal)++;
        printf("   %%%lu = OP_CMP_NOT_EQ %%%lu, 0\n", new_temporal, derefd.temporal.val);

        if (new_temporal + 1 > temporals_map->cap) {
            TC_Entry_LIST_inflate(temporals_map, new_temporal + 5);
        }
        temporals_map->array[new_temporal] = (TC_Entry){
            .type = (TC_typeid){
                .ty = TC_TY_Primitive,
                .info.primitive = TC_Primitive__Bool,
                // .full_typeuse_span = (TokenSpan){.sv = SV_from_str_len("Bool", 4)}
            },
            .known_temporal = true,
            .is_value_known = false,
            .is_addressable = false,
        };

        return (IR_temporal){.val = new_temporal};
    }

    INTERNAL_ERROR("typechecking error here....");
}


IR_temporal_and_TC_type perf_auto_ref(
    const IR_temporal* intake,
    const TC_typeid* intake_type,
    size_t* highest_temporal,
    TC_Entry_LIST* temporals_map
) {
    const uint64_t new_temporal = (*highest_temporal)++;
    printf("   %%%lu = REF_OF %%%lu\n", new_temporal, intake->val);

    const TC_typeid nt = (TC_typeid){
        .ty = TC_TY_Ref,
        .info.contained = (TC_typeid *) intake_type,
        // .full_typeuse_span = (TokenSpan){.sv = intake_type->full_typeuse_span.sv}
    };

    if (new_temporal + 1 > temporals_map->cap) {
        TC_Entry_LIST_inflate(temporals_map, new_temporal + 5);
    }
    temporals_map->array[new_temporal] = (TC_Entry){
        .type = nt,
        .known_temporal = true,
        .is_value_known = false,
        .is_addressable = false,
    };

    return (IR_temporal_and_TC_type){.temporal = (IR_temporal){.val = new_temporal}, .type = nt};
}


IR_temporal perf_autos(
    const IR_temporal* restrict intake,
    const TC_typeid* intake_type,
    const TC_typeid* output_type,
    size_t* restrict highest_temporal,
    const khash_t(sv_u64_map)* restrict identifier_map,
    const TC_TypeTable* nct,
    TC_Entry_LIST* restrict temporals_map,
    const SourceInfo* restrict source_info,
    const SV* value_token,
    TC_Value_LIST* temporals_map_values,
    const IR_temporal_LIST* promoted,
    const TargetInfo* restrict target_info
) {
    if (is_type_eq(intake_type, output_type)) {
        return use_value(*intake, highest_temporal, temporals_map, promoted, nct, target_info);
    }

//     uint64_t current_temp = intake->val;

//     uint64_t current_temp = use_value(*intake, highest_temporal, temporals_map).val;
    const bool wants_auto_ref =
        output_type->ty == TC_TY_Ref &&
        intake_type->ty != TC_TY_Ref &&
        is_type_eq(output_type->info.contained, intake_type);

    uint64_t current_temp = wants_auto_ref
        ? intake->val
        : use_value(*intake, highest_temporal, temporals_map, promoted, nct, target_info).val;


    TC_typeid current_type = *intake_type;
    while (current_type.ty == TC_TY_Ref) {
        const uint64_t new_temporal = (*highest_temporal)++;
        printf("   %%%lu = READ_PTR %%%lu\n", new_temporal, current_temp);
        current_temp = new_temporal;
        current_type = *current_type.info.contained;
        if (temporals_map->cap < current_temp + 1) {
            TC_Entry_LIST_inflate(temporals_map, current_temp + 5);
        }
        temporals_map->array[current_temp] = (TC_Entry){
            .known_temporal = true,
            .type = current_type,
            .is_value_known = false,
            .is_addressable = true,
        };
    }

    if (is_type_eq(&current_type, output_type)) {
        return (IR_temporal){.val = current_temp};
    }

    const TC_typeid* target_base = (output_type->ty == TC_TY_Ref) ? (output_type->info.contained) : output_type;

    // Check standard widening OR constant narrowing
    bool can_widen = can_be_implicit_casted(&current_type, target_base);
    bool can_narrow = !can_widen && can_const_narrow(current_temp, target_base, temporals_map, temporals_map_values);

    // if (!is_type_eq(&current_type, target_base) && can_be_implicit_casted(&current_type, target_base)) {
    //     const uint64_t new_temp = (*highest_temporal)++;
    //     const SV target_base_sv = format_TC_typeid(target_base, identifier_map, nct);
    //     printf("   %%%lu = CAST<"SV_format">(%%%lu) [IMPLICIT]\n", new_temp, SV_v_args(target_base_sv), current_temp);
    //     current_type = *target_base;
    //     current_temp = new_temp;
    //
    //     if (temporals_map->cap < current_temp+1) {
    //         TC_Entry_LIST_inflate(temporals_map, current_temp+1);
    //     }
    //     temporals_map->array[current_temp] = (TC_Entry) {
    //         .known_temporal = true,
    //         .type = current_type,
    //         .is_value_known = false,
    //         .is_addressable = false,
    //     };
    // }
    if (!is_type_eq(&current_type, target_base) && (can_widen || can_narrow)) {
        const uint64_t new_temp = (*highest_temporal)++;
        const SV target_base_sv = format_TC_typeid(target_base, identifier_map, nct);

        // Print different debug info if it was a constant narrow
        if (can_narrow) {
            printf("   %%%lu = CAST<"SV_format">(%%%lu) [IMPLICIT CONST-NARROW]\n", new_temp, SV_v_args(target_base_sv),
                   current_temp);
        } else {
            printf("   %%%lu = CAST<"SV_format">(%%%lu) [IMPLICIT]\n", new_temp, SV_v_args(target_base_sv),
                   current_temp);
        }

        if (temporals_map->cap < new_temp + 1) {
            TC_Entry_LIST_inflate(temporals_map, new_temp + 1);
        }

        if (temporals_map_values->cap < new_temp + 1) {
            TC_Value_LIST_inflate(temporals_map_values, new_temp + 1);
        }

        // CRITICAL: Propagate the known value!
        TC_Entry old_entry = temporals_map->array[current_temp];
        temporals_map->array[new_temp] = (TC_Entry){
            .known_temporal = true,
            .type = *target_base,
            .is_value_known = old_entry.is_value_known,
            // && temporals_map_values->array[current_temp].kind == TC_VK_Int,
            .is_addressable = false,
        };

        // Copy the actual value over!
        // if (old_entry.is_value_known && temporals_map_values->array[current_temp].kind == TC_VK_Int) {
        //     temporals_map_values->array[new_temp] = (TC_Value) {
        //         .kind = TC_VK_Int,
        //         .Int = temporals_map_values->array[current_temp].Int
        //     };
        // }
        if (old_entry.is_value_known) {
            temporals_map_values->array[new_temp] = temporals_map_values->array[current_temp];
        }


        current_type = *target_base;
        current_temp = new_temp;
    }

    if (is_type_eq(&current_type, output_type)) {
        return (IR_temporal){.val = current_temp};
    }

    // auto-ref
    if (output_type->ty == TC_TY_Ref && is_type_eq(output_type->info.contained, &current_type)) {
        if (!temporals_map->array[current_temp].is_addressable) {
            const SV msg = SV_from_str_len("cannot take reference of temporary value", 40);
            type_error(&source_info->source, &source_info->source_filename, value_token, &msg);
            const SV hint = SV_from_str_len("consider creating variable with that value and than using it here", 0);
            hint_standalone(&hint);
            exit(1);
        }
        const uint64_t new_temporal = (*highest_temporal)++;
        printf("   %%%lu = REF_OF %%%lu\n", new_temporal, current_temp);
        current_type = *output_type;
        current_temp = new_temporal;

        if (temporals_map->cap < current_temp + 1) {
            TC_Entry_LIST_inflate(temporals_map, current_temp + 5);
        }
        temporals_map->array[current_temp] = (TC_Entry){
            .known_temporal = true,
            .type = current_type,
            .is_value_known = false,
            .is_addressable = false,
        };
    }

    if (is_type_eq(output_type, &current_type)) {
        return (IR_temporal){.val = current_temp};
    } else {
        // Type mismatch! Could not coerce...
        const SV in_type = format_TC_typeid(intake_type, identifier_map, nct);
        const SV out_type = format_TC_typeid(output_type, identifier_map, nct);
        const size_t MAX_SIZE = 1024;
        char* r_msg = malloc(sizeof(char) * MAX_SIZE);
        int len = snprintf(r_msg, MAX_SIZE,
                           "Type mismatch! Could not coerce "C_BOLD""SV_format""C_RESET" into "C_BOLD""SV_format""
                           C_RESET"", SV_v_args(in_type), SV_v_args(out_type));
        const SV msg = SV_from_str_len(r_msg, len);
        type_error(&source_info->source, &source_info->source_filename, value_token, &msg);
        exit(1);
    }
}