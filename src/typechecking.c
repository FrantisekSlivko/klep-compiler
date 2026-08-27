#include "typechecking.h"
#include "error.h"
#include "utils/dev.h"

/*
 * this file contains two important passes: type completion and typechecking
 *
 * 1. the type completion is where struct layout and type size is calculated
 *    (! when generics are used, they are calculated per use case and cached)
 *
 *
 */


void print_TC_typeid(const TC_typeid* restrict ty, khash_t(sv_u64_map)* restrict identifier_map, const TC_TypeTable* restrict tt) {
    switch (ty->ty) {
        case TC_TY_Primitive: {
            const SV k = TC_Primitive_TY_to_SV(ty->info.primitive);
            printf(SV_format, SV_v_args(k));
            break;
        }
        case TC_TY_Struct: {
            const SV name = SV_from_identifiermap_lookup(identifier_map, tt->structs.array[ty->info.type_id].name);
            printf(SV_format"{%u}", SV_v_args(name), ty->info.type_id);
            break;
        }
        case TC_TY_Enum: {
            const SV name = SV_from_identifiermap_lookup(identifier_map, tt->enums.array[ty->info.type_id].name);
            printf(SV_format, SV_v_args(name));
            break;
        }
        case TC_TY_Ref: {
            printf("Ref<");
            print_TC_typeid(ty->info.contained, identifier_map, tt);
            putc('>', stdout);
            break;
            break;
        }
        case TC_TY_Array: {
            printf("Array<");
            print_TC_typeid(ty->info.contained, identifier_map, tt);
            putc('>', stdout);
            break;
        }
        case TC_TY_List: {
            printf("List<");
            print_TC_typeid(ty->info.contained, identifier_map, tt);
            putc('>', stdout);
            break;
        }
        case TC_TY_Box: {
            printf("Box<");
            print_TC_typeid(ty->info.contained, identifier_map, tt);
            putc('>', stdout);
            break;
        }
        case TC_TY_Generic: {
            const SV name = SV_from_identifiermap_lookup(identifier_map, ty->info.generic.generics_name);
            printf(SV_format"<", SV_v_args(name));
            for (size_t n=0; n < ty->info.generic.contained_len; n++) {
                print_TC_typeid(&ty->info.generic.contained[n], identifier_map, tt);
                if (n+1 != ty->info.generic.contained_len) {
                    printf(", ");
                }
            }
            putc('>', stdout);
            break;
        }
    }
}



/// returns false on fail
bool TC_Primitive_TY_from_u32(const uint32_t num, TC_Primitive_TY* restrict out) {
    if (num >= 1 && num <= 11) {
        *out = (TC_Primitive_TY)num;
        return true;
    }
    return false;
}

/// returns false on fail
bool TC_Primitive_TY_from_SV(const SV* sv, TC_Primitive_TY* restrict out) {
    if (Token_cmp(sv, "Int8")) {
        *out = TC_Primitive__Int8;
        return true;
    }
    if (Token_cmp(sv, "Int16")) {
        *out = TC_Primitive__Int16;
        return true;
    }
    if (Token_cmp(sv, "Int32")) {
        *out = TC_Primitive__Int32;
        return true;
    }
    if (Token_cmp(sv, "Int64")) {
        *out = TC_Primitive__Int64;
        return true;
    }
    if (Token_cmp(sv, "Uint8")) {
        *out = TC_Primitive__Uint8;
        return true;
    }
    if (Token_cmp(sv, "Uint16")) {
        *out = TC_Primitive__Uint16;
        return true;
    }
    if (Token_cmp(sv, "Uint32")) {
        *out = TC_Primitive__Uint32;
        return true;
    }
    if (Token_cmp(sv, "Uint64")) {
        *out = TC_Primitive__Uint64;
        return true;
    }
    if (Token_cmp(sv, "Float32")) {
        *out = TC_Primitive__Float32;
        return true;
    }
    if (Token_cmp(sv, "Float64")) {
        *out = TC_Primitive__Float64;
        return true;
    }
    if (Token_cmp(sv, "Char")) {
        *out = TC_Primitive__Char;
        return true;
    }
    return false;
}

uint8_t TC_Primitive_TY_size(const TC_Primitive_TY pt) {
    switch (pt) {
        case TC_Primitive__Int8: {
            return 1;
        }
        case TC_Primitive__Int16: {
            return 2;
        }
        case TC_Primitive__Int32: {
            return 4;
        }
        case TC_Primitive__Int64: {
            return 8;
        }
        case TC_Primitive__Uint8: {
            return 1;
        }
        case TC_Primitive__Uint16: {
            return 2;
        }
        case TC_Primitive__Uint32: {
            return 4;
        }
        case TC_Primitive__Uint64: {
            return 8;
        }
        case TC_Primitive__Float32: {
            return 4;
        }
        case TC_Primitive__Float64: {
            return 8;
        }
        case TC_Primitive__Char: {
            return 1;
        }
    }
    INTERNAL_ERROR("invalid control path for TC_Primitive_TY sizeof | %u", (uint32_t)pt)
}

uint32_t TC_Primitive_TY_to_u32(const TC_Primitive_TY pt) {
    return (uint32_t)pt;
}

SV TC_Primitive_TY_to_SV(const TC_Primitive_TY pt) {
    switch (pt) {
        case TC_Primitive__Int8: {
            return SV_from_str_len("Int8", 4);
        }
        case TC_Primitive__Int16: {
            return SV_from_str_len("Int16", 5);
        }
        case TC_Primitive__Int32: {
            return SV_from_str_len("Int32", 5);
        }
        case TC_Primitive__Int64: {
            return SV_from_str_len("Int64", 5);
        }
        case TC_Primitive__Uint8: {
            return SV_from_str_len("Uint8", 5);
        }
        case TC_Primitive__Uint16: {
            return SV_from_str_len("Uint16", 6);
        }
        case TC_Primitive__Uint32: {
            return SV_from_str_len("Uint32", 6);
        }
        case TC_Primitive__Uint64: {
            return SV_from_str_len("Uint64", 6);
        }
        case TC_Primitive__Float32: {
            return SV_from_str_len("Float32", 7);
        }
        case TC_Primitive__Float64: {
            return SV_from_str_len("Float64", 7);
        }
        case TC_Primitive__Char: {
            return SV_from_str_len("Char", 4);
        }
    }
    return SV_from_str("UNKNOWN");
}


init_list_type(size_t, size_t_LIST)

/// returns TRUE if EQUAL
bool CMP_TC_typeid(const TC_typeid* restrict a, const TC_typeid* restrict b) {
    if (a->ty != b->ty) return false;
    switch (a->ty) {
        case TC_TY_Primitive: {
            return a->info.primitive == b->info.primitive;
        }
        case TC_TY_Struct:
        case TC_TY_Enum: {
            return a->info.type_id == b->info.type_id;
        }
        case TC_TY_Ref:
        case TC_TY_Array:
        case TC_TY_List:
        case TC_TY_Box: {
            return CMP_TC_typeid(a->info.contained, b->info.contained);
        }
        case TC_TY_Generic: {
            if (a->info.generic.generics_name != b->info.generic.generics_name) return false;
            if (a->info.generic.contained_len != b->info.generic.contained_len) return false;
            for (size_t n=0; n<a->info.generic.contained_len; n++) {
                const TC_typeid* n_a = &a->info.generic.contained[n];
                const TC_typeid* n_b = &b->info.generic.contained[n];
                if (!CMP_TC_typeid(n_a, n_b)) {
                    return false;
                }
            }
            return true;
        }
    }
    abort();
}

/*

STRATEGY
1st pass -> structs + enums -> List<(Name, Kind, index_into_out)>
 + expand out_enums+out_structs to fit the indexes, but do not populate them

2nd pass = resolution



 */


// /// either find the type and return ref to where the type info is stored,
// /// or create a placeholder to be filled later and return ref to that
// TC_typeid get_type(const khash_t(sv_u64_map)* restrict identifier_map,
//     TC_structdef_LIST* restrict structs,
//     TC_enumdef_LIST* restrict enums,
//     /// for normal enums and structs
//     Identifier_LIST* restrict incomplete_type_names,
//
//     IR_generic_structdef_LIST* restrict generic_structs,
//     IR_generic_enumdef_LIST* restrict generic_enums,
//
//     // TC_generic_materialization_indexpair_LIST* restrict generic_materializations,
//
//     // TC_known_name_enumstruct_type_LIST* restrict known_structs,
//     // TC_known_name_enumstruct_type_LIST* restrict known_enums,
//
//     const IR_typeuse* restrict type
// ) {
//     switch (type->tt_ty) {
//         case IR_TY_Primitive: {
//             return (TC_typeid){
//                 .ty = TC_TY_Primitive,
//                 .info.primitive = type->info.primitive
//             };
//         }
//         case IR_TY_Named: {
//             // hmm, maybe make a separate index for named normal struct -> index in structs
//             // and put materialized things there, but wait, now I cannot cache materialized types??
//
//             // structs and enums should remain as a list for computed types,
//             // but search by name and by materialization of generics should be another two indexes
//
//             // search existing ones if not found, make an incomplete one
//             for (size_t n = 0; n < known_structs->len; n++) {
//                 if (type->info.name == known_structs->array[n].name) {
//                     return (TC_typeid){
//                         .ty = TC_TY_Struct,
//                         .info.type_id = known_structs->array[n].index
//                     };
//                 }
//             }
//             for (size_t n = 0; n < known_enums->len; n++) {
//                 if (type->info.name == known_enums->array[n].name) {
//                     return (TC_typeid) {
//                         .ty = TC_TY_Enum,
//                         .info.type_id = known_enums->array[n].index
//                     };
//                 }
//             }
//             // TC_structdef_LIST_push(structs, (TC_structdef){.name = type->info.name});
//             // bool found = false;
//             // for (size_t n = 0; n < incomplete_structs_indexes->len; n++) {
//             //     if (structs->array[incomplete_structs_indexes->array[n]].name == type->info.name) {
//             //         found = true;
//             //         break;
//             //     }
//             // }
//
//             // search in incomplete
//             for (size_t n = 0; n < incomplete_type_names->len; n++) {
//                 if (incomplete_type_names->array[n] == type->info.name) {
//                     // found
//                     return (TC_typeid) {
//                         .ty = TC_TY_Indirect_Named_Type,
//                         .info.indirect = {
//                             type->info.name,
//                             (uint32_t)n
//                         }
//                     };
//                 }
//             }
//
//             const uint32_t nth = (uint32_t)incomplete_type_names->len;
//             Identifier_LIST_push(incomplete_type_names, type->info.name);
//
//             // const size_t id = structs->len - 1;
//             // if (!found) {
//                 // size_t_LIST_push(incomplete_structs_indexes, id);
//             // }
//             // return (TC_typeid){
//                 // .ty = TC_TY_Struct,
//                 // .info.type_id = id,
//             // };
//             return (TC_typeid) {
//                 .ty = TC_TY_Indirect_Named_Type,
//                 .info.indirect = {
//                     type->info.name,
//                     nth
//                 }
//             };
//         }
//         case IR_TY_Ref:
//         case IR_TY_Array:
//         case IR_TY_Box:
//         case IR_TY_List: {
//             TC_typeid* contained = malloc(sizeof(TC_typeid));
//             *contained = get_type(identifier_map, structs, enums, incomplete_type_names, type->contained);
//
//             free(type->contained);
//
//             TC_type_ty ty;
//             switch (type->tt_ty) {
//                 case IR_TY_Ref: {
//                     ty = TC_TY_Ref;
//                     break;
//                 }
//                 case IR_TY_Array: {
//                     ty = TC_TY_Array;
//                     break;
//                 }
//                 case IR_TY_List: {
//                     ty = TC_TY_List;
//                     break;
//                 }
//                 case IR_TY_Box: {
//                     ty = TC_TY_Box;
//                     break;
//                 }
//                 default: {
//                     fprintf(stderr, "[internal] whut? what happened to control flow? | u402\n");
//                     exit(3);
//                 }
//             }
//
//             return (TC_typeid){
//                 .ty = ty,
//                 .info.contained = contained
//             };
//         }
//         case IR_TY_Generic: {
//             // fprintf(stderr, "[WARNING] IR_Type to TC_type for GENERICS unimplemented\n");
//
//             TC_typeid* contained = malloc(sizeof(TC_typeid)*type->contained_len);
//             for (size_t n = 0; n < type->contained_len; n++) {
//                 const TC_typeid tc = get_type(identifier_map, structs, enums, incomplete_structs_indexes, &type->contained[n]);
//                 contained[n] = tc;
//             }
//
//             return (TC_typeid) {
//                 .ty = TC_TY_Generic,
//                 .info.generic = {
//                     .contained = contained,
//                     .contained_len = type->contained_len,
//                     .generics_name = type->info.name,
//                 },
//             };
//         }
//     }
//
//     // return ();
//     fprintf(stderr, "[internal] whut? what happened to control flow? | u403\n");
//     exit(3);
// }
//
//
// // typedef enum { UNVISITED, IN_STACK, DONE } topo_state;
// typedef enum { UNVISITED, IN_STACK, DONE, ERROR } topo_state;
//
// void dfs(const uint32_t idx, const TC_structdef_LIST* restrict structs,
//          topo_state* restrict state, uint32_t* restrict order, uint32_t* restrict order_len,
//          const khash_t(sv_u64_map)* restrict identifier_map, const SourceInfo* restrict source_info)
// {
//     if (state[idx] == DONE || state[idx] == ERROR) return;
//     if (state[idx] == IN_STACK) {
//         // state[idx] = ERROR; return;
//         const SV* name = &structs->array[idx].raw_name;
//         const SV msg = SV_from_str("owned cycles are forbidden, use Ref<T> if you need to construct cycle in runtime");
//         syntax_error(&source_info->source, &source_info->source_filename, name, &msg);
//         // fprintf(stderr, "error: cyclic struct '"SV_format"'\n", SV_v_args(name));
//         // abort();
//         exit(1);
//     }
//
//     state[idx] = IN_STACK;
//     const TC_structdef* s = &structs->array[idx];
//
//     for (uint32_t i = 0; i < s->fields.len; i++) {
//         const TC_typeid* ft = &s->fields.array[i].type;
//         if (ft->ty == TC_TY_Struct) {
//             dfs(ft->info.type_id, structs, state, order, order_len, identifier_map, source_info);
//             if (state[ft->info.type_id] == ERROR) {
//                 state[idx] = ERROR; // propagate
//                 return;
//             }
//         }
//     }
//
//     state[idx] = DONE;
//     order[(*order_len)++] = idx;
// }
//
// uint32_t size_of(const TC_typeid* restrict t, const TC_struct_layout* restrict layouts, const uint32_t ptr_size) {
//     switch (t->ty) {
//         case TC_TY_Primitive: return TC_Primitive_TY_size(t->info.primitive);
//         case TC_TY_Struct:    return layouts[t->info.type_id].size;
//
//         case TC_TY_Ref:
//         case TC_TY_Box:       return ptr_size;
//
//         case TC_TY_Array:     return ptr_size * 2;
//         case TC_TY_List:      return ptr_size * 3;
//         case TC_TY_Generic: {
//             UNIMPLEMENTED("generics size")
//             return -1;
//         }
//
//     }
//     abort();
// }
//
// uint32_t align_of(const TC_typeid* restrict t, const TC_struct_layout* restrict layouts, const uint32_t ptr_size) {
//     switch (t->ty) {
//         case TC_TY_Primitive: {
//             return TC_Primitive_TY_size(t->info.primitive); // holds for standard primitives
//         }
//         case TC_TY_Struct: {
//             return layouts[t->info.type_id].align;
//         }
//         case TC_TY_Ref:
//         case TC_TY_Array:
//         case TC_TY_List:
//         case TC_TY_Box: {
//             return ptr_size;
//         }
//         case TC_TY_Generic:
//             UNIMPLEMENTED("generics align")
//             return -1;
//     }
//     abort();
// }
//
// void compute_layout(const uint32_t idx, const TC_structdef_LIST* restrict structs, TC_struct_layout* restrict layouts, const uint32_t ptr_size) {
//     const TC_structdef* s = &structs->array[idx];
//     TC_struct_layout* L = &layouts[idx];
//     L->field_offsets = malloc(s->fields.len * sizeof(uint32_t));
//
//     uint32_t offset = 0, max_align = 1;
//
//     for (uint32_t i = 0; i < s->fields.len; i++) {
//         const TC_typeid* ft = &s->fields.array[i].type;
//         const uint32_t fa = align_of(ft, layouts, ptr_size);
//         const uint32_t fs = size_of(ft, layouts, ptr_size);
//
//         offset = (offset + fa - 1) & ~(fa - 1); // align up
//         L->field_offsets[i] = offset;
//         offset += fs;
//         if (fa > max_align) max_align = fa;
//     }
//
//     L->align = max_align;
//     L->size  = (offset + max_align - 1) & ~(max_align - 1); // tail padding
// }

/// returns TRUE if a is equal b
bool CMP_IR_typeuse(const IR_typeuse* restrict a, const IR_typeuse* restrict b) {
    if (a->tt_ty != b->tt_ty) return false;
    switch (a->tt_ty) {
        case IR_TY_Primitive: {
            return a->info.primitive == b->info.primitive;
        }
        case IR_TY_Named: {
            return a->info.named.name == b->info.named.name;
        }
        case IR_TY_Ref:
        case IR_TY_Box:
        case IR_TY_Array:
        case IR_TY_List: {
            return CMP_IR_typeuse(a->contained, b->contained);
        }
        case IR_TY_Generic: {
            if (a->contained_len != b->contained_len) return false;
            for(size_t n=0; n<a->contained_len; n++) {
                if (!CMP_IR_typeuse(&a->contained[n], &b->contained[n])) {
                    return false;
                }
            }
            return true;
        }
    }
    INTERNAL_ERROR("should be unreachable")
}

IR_typeuse substitute_typeuse(
    const IR_typeuse* restrict input,
    const Identifier_LIST* restrict generic_args,
    const IR_typeuse* restrict args
) {
    switch (input->tt_ty) {
        case IR_TY_Primitive: {
            return *input;
        }
        case IR_TY_Named: {
            for (size_t g = 0; g < generic_args->len; g++) {
                if (input->info.named.name == generic_args->array[g]) {
                    printf("NKL %i\n", args[g].tt_ty);
                    return args[g];
                }
            }

            return *input;
        }
        case IR_TY_Ref:
        case IR_TY_Box:
        case IR_TY_Array:
        case IR_TY_List: {

            if (input->contained_len != 1) {
                INTERNAL_ERROR("invalid contained length of builtin type")
            }

            IR_typeuse* new_contained = malloc(sizeof(IR_typeuse));
            *new_contained = substitute_typeuse(input->contained, generic_args, args);
            free(input->contained);

            return (IR_typeuse) {
                .tt_ty = input->tt_ty,
                .info = input->info,
                .contained_len = input->contained_len,
                .contained = new_contained,
            };
        }
        case IR_TY_Generic: {

            IR_typeuse* new_contained = malloc(sizeof(IR_typeuse)*input->contained_len);
            for(size_t n=0; n<input->contained_len; n++) {
                new_contained[n] = substitute_typeuse(&input->contained[n], generic_args, args);
            }

            free(input->contained);

            return (IR_typeuse) {
                .tt_ty = IR_TY_Generic,
                .info = input->info,
                .contained_len = input->contained_len,
                .contained = new_contained,
            };
            // UNIMPLEMENTED("substitute_typeuse for IR_TY_Generic")
        }
    }

}


TC_typeid resolve_type(const IR_typeuse* restrict type,
    const TC_name_index_record_LIST* restrict name_index,
    const SourceInfo* restrict source_info,
    TC_materialized_generic_index_record_LIST* restrict materialized_generics,
    const IR_incomplete_TypeTable* restrict table,
    TC_structdef_LIST* restrict out_structs,
    TC_enumdef_LIST* restrict out_enums
) {
    switch (type->tt_ty) {
        case IR_TY_Primitive: {
            return (TC_typeid){
                .ty = TC_TY_Primitive,
                .info.primitive = type->info.primitive
            };
        }
        case IR_TY_Named: {
            for (size_t n=0; n<name_index->len; n++) {
                if (name_index->array[n].name == type->info.named.name) {
                    TC_type_ty ty = TC_TY_Struct;
                    switch (name_index->array[n].kind) {
                        case TC_NIRK_Enum: {
                            ty = TC_TY_Enum;
                            break;
                        }
                        case TC_NIRK_Struct: {
                            ty = TC_TY_Struct;
                            break;
                        }
                    }
                    return (TC_typeid) {
                        .ty = ty,
                        .info.type_id = name_index->array[n].nth,
                    };
                }
            }

            const SV msg = SV_from_str("undeclared type");
            syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.raw_name, &msg);
            exit(1);
        }
        case IR_TY_Ref:
        case IR_TY_Array:
        case IR_TY_Box:
        case IR_TY_List: {
            TC_typeid* contained = malloc(sizeof(TC_typeid));
            *contained = resolve_type(type->contained, name_index, source_info, materialized_generics, table, out_structs, out_enums);

            free(type->contained);

            TC_type_ty ty;
            switch (type->tt_ty) {
                case IR_TY_Ref: {
                    ty = TC_TY_Ref;
                    break;
                }
                case IR_TY_Array: {
                    ty = TC_TY_Array;
                    break;
                }
                case IR_TY_List: {
                    ty = TC_TY_List;
                    break;
                }
                case IR_TY_Box: {
                    ty = TC_TY_Box;
                    break;
                }
                default: {
                    INTERNAL_ERROR("whut? what happened to control flow? | u402")
                }
            }

            return (TC_typeid){
                .ty = ty,
                .info.contained = contained
            };
        }
        case IR_TY_Generic: {
            // check if materialized:
            // materialized_generics
            for (size_t n=0; n<materialized_generics->len; n++) {
                const TC_materialized_generic_index_record mr = materialized_generics->array[n];
                if (mr.generic.tt_ty != IR_TY_Generic) {
                    INTERNAL_ERROR("should never happen | u403")
                }
                const IR_typeuse tu = mr.generic;
                if (tu.info.named.name != type->info.named.name) {
                    continue;
                }
                if (tu.contained_len != type->contained_len) {
                    continue;
                }
                bool ineq = false;
                for (size_t j=0; j<tu.contained_len; j++) {
                    if (!CMP_IR_typeuse(&tu.contained[j], &type->contained[j])) {
                        ineq = true;
                        break;
                    }
                }
                if (ineq) {
                    continue;
                }

                // use this, it is this one
                TC_type_ty ty = TC_TY_Struct;
                switch (mr.kind) {
                    case TC_NIRK_Enum: {
                        ty = TC_TY_Enum;
                        break;
                    }
                    case TC_NIRK_Struct: {
                        ty = TC_TY_Struct;
                        break;
                    }
                }
                return (TC_typeid) {
                    .ty = ty,
                    .info.type_id = mr.nth,
                };
            }

            // materialize if it is existing generic
            // 1. check if generic by that name exists
            // 2. materialize

            // MATERIALIZE ENUM
            for (size_t k=0; k<table->generic_enums.len; k++) {
                const IR_generic_enumdef ged = table->generic_enums.array[k];
                if (ged.name != type->info.named.name) continue;

                // is correct number of type args used?
                if (ged.generic_args.len != type->contained_len) {
                    char* raw_msg = calloc(1, 100);
                    snprintf(raw_msg, 100, "expected %lu type arguments, but found only %lu", ged.generic_args.len, type->contained_len);
                    const SV msg = SV_from_str(raw_msg);
                    syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.raw_name, &msg);
                    free(raw_msg);
                    exit(1);
                }

                // materialize
                TC_enumdef_variant_LIST new_variants = TC_enumdef_variant_LIST_new_with_capacity(ged.variants.len);
                for (size_t j=0; j<ged.variants.len; j++) {
                    const IR_enumdef_variant variant = ged.variants.array[j];
                    TC_typeid* new_values = malloc(sizeof(TC_typeid)*variant.values_len);

                    for (size_t v=0; v<variant.values_len; v++) {
                        const IR_typeuse substituted = substitute_typeuse(&variant.values[v], &ged.generic_args, type->contained);
                        const TC_typeid ty = resolve_type(&substituted, name_index, source_info, materialized_generics, table, out_structs, out_enums);
                        new_values[v] = ty;
                    }

                    TC_enumdef_variant_LIST_push(&new_variants, (TC_enumdef_variant) {
                        variant.name,
                        variant.raw_name,
                        new_values,
                        variant.values_len
                    });
                }

                const TC_enumdef new_enumdef = (TC_enumdef) {
                    ged.name,
                    ged.raw_name,
                    new_variants,
                    0 // TODO: CALC SIZE
                };

                const size_t nth = out_enums->len;
                TC_enumdef_LIST_push(out_enums, new_enumdef);
                TC_materialized_generic_index_record_LIST_push(materialized_generics, (TC_materialized_generic_index_record) {
                    *type,
                    TC_NIRK_Enum,
                    nth
                });

                return (TC_typeid) {
                    .ty = TC_TY_Enum,
                    .info.type_id = nth,
                };
            }

            // MATERIALIZE STRUCT
            for (size_t k=0; k<table->generic_structs.len; k++) {
                const IR_generic_structdef ges = table->generic_structs.array[k];
                if (ges.name != type->info.named.name) continue;

                // is correct number of type args used?
                if (ges.generic_args.len != type->contained_len) {
                    char* raw_msg = calloc(1, 100);
                    snprintf(raw_msg, 100, "expected %lu type arguments, but found only %lu", ges.generic_args.len, type->contained_len);
                    const SV msg = SV_from_str(raw_msg);
                    syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.raw_name, &msg);
                    free(raw_msg);
                    exit(1);
                }

                TC_field_LIST new_fields = TC_field_LIST_new_with_capacity(ges.fields.len);
                new_fields.len = ges.fields.len;

                for (size_t j=0; j<ges.fields.len; j++) {
                    const IR_field fd = ges.fields.array[j];
                    const IR_typeuse substituted = substitute_typeuse(&fd.type, &ges.generic_args, type->contained);
                    const TC_typeid ty = resolve_type(&substituted, name_index, source_info, materialized_generics, table, out_structs, out_enums);
                    new_fields.array[j] = (TC_field) {
                        fd.name,
                        fd.raw_name,
                        ty
                    };
                }

                const TC_structdef new_structdef = (TC_structdef) {
                    ges.name,
                    ges.raw_name,
                    new_fields,
                    (TC_struct_layout) { // TODO: calc
                        0,
                        0,
                        NULL
                    }
                };

                const size_t nth = out_structs->len;
                TC_structdef_LIST_push(out_structs, new_structdef);
                TC_materialized_generic_index_record_LIST_push(materialized_generics, (TC_materialized_generic_index_record) {
                    *type,
                    TC_NIRK_Struct,
                    nth
                });

                return (TC_typeid) {
                    .ty = TC_TY_Struct,
                    .info.type_id = nth,
                };
            }

            // by now, we know, it does not exist
            const SV msg = SV_from_str("unknow generic");
            syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.raw_name, &msg);
            exit(1);


        }
    }

}

TC_TypeTable close_type_table(const IR_incomplete_TypeTable* restrict table,
                              const khash_t(sv_u64_map)* restrict identifier_map,
                              const TargetInfo* restrict target_info,
                              const SourceInfo* restrict source_info) {

    // uint32_t id_counter_for_structs = 0;
    // uint32_t id_counter_for_enums = 0;

    TC_name_index_record_LIST name_index = TC_name_index_record_LIST_new_with_capacity(table->enums.len + table->structs.len);

    for (size_t n=0; n<table->enums.len; n++) {
        const IR_enumdef ed = table->enums.array[n];
        for (size_t k=0; k<name_index.len; k++) {
            if (name_index.array[k].name == ed.name) {
                const SV msg = SV_from_str("type name collision");
                syntax_error(&source_info->source, &source_info->source_filename, &ed.raw_name, &msg);
                exit(1);
            }
        }
        TC_name_index_record_LIST_push(&name_index, (TC_name_index_record) {
            ed.name,
            TC_NIRK_Enum,
            n
        });
    }

    for (size_t n=0; n<table->structs.len; n++) {
        const IR_structdef sd = table->structs.array[n];
        for (size_t k=0; k<name_index.len; k++) {
            if (name_index.array[k].name == sd.name) {
                const SV msg = SV_from_str("type name collision");
                syntax_error(&source_info->source, &source_info->source_filename, &sd.raw_name, &msg);
                exit(1);
            }
        }
        TC_name_index_record_LIST_push(&name_index, (TC_name_index_record) {
            sd.name,
            TC_NIRK_Struct,
            n
        });
    }




    TC_structdef_LIST out_structs = TC_structdef_LIST_new_with_capacity(table->structs.len);
    out_structs.len = table->structs.len;

    TC_enumdef_LIST out_enums = TC_enumdef_LIST_new_with_capacity(table->enums.len);
    out_enums.len = table->enums.len;

    TC_materialized_generic_index_record_LIST materialized_generics = TC_materialized_generic_index_record_LIST_new();

    // TC_materialized_generic_structdef_LIST materialized_generic_structs = TC_materialized_generic_structdef_LIST_new();
    // TC_materialized_generic_enumdef_LIST materialized_generic_enums = TC_materialized_generic_enumdef_LIST_new();
    // TODO: make index like the one with names, but for generics

    {
        for (size_t n=0; n<table->enums.len; n++) {

            const IR_enumdef ed = table->enums.array[n];
            TC_enumdef_variant_LIST new_variants = TC_enumdef_variant_LIST_new_with_capacity(ed.variants.len);

            for (size_t k=0; k< ed.variants.len; k++) {
                const IR_enumdef_variant variant = ed.variants.array[k];

                for (size_t j=0; j < k; j++) {
                    if (variant.name == ed.variants.array[j].name) {
                        const SV msg = SV_from_str("variant name must be unique");
                        syntax_error(&source_info->source, &source_info->source_filename, &variant.raw_name, &msg);
                        exit(1);
                    }
                }

                TC_typeid* new_values = NULL;

                if (variant.values_len > 0) {
                    new_values = malloc(sizeof(TC_typeid)*variant.values_len);
                    for (size_t j=0; j<variant.values_len; j++) {
                        const IR_typeuse tu = variant.values[j];
                        const TC_typeid new_t = resolve_type(&tu, &name_index, source_info, &materialized_generics, table, &out_structs, &out_enums);
                        new_values[j] = new_t;
                    }
                }


                const TC_enumdef_variant new_variant = (TC_enumdef_variant) {
                    variant.name,
                    variant.raw_name,
                    new_values,
                    variant.values_len
                };
                TC_enumdef_variant_LIST_push(&new_variants, new_variant);
            }

            out_enums.array[n] = (TC_enumdef) {
                ed.name,
                ed.raw_name,
                new_variants,
                0 // TODO: CALC SIZE (variant count -> next 2^n + largest value of variants)
            };
        }

        for (size_t n=0; n<table->structs.len; n++) {
            const IR_structdef sd = table->structs.array[n];
            // sd.fields
            TC_field_LIST new_fields = TC_field_LIST_new_with_capacity(sd.fields.len);
            for (size_t k=0; k<sd.fields.len; k++) {
                const IR_field field = sd.fields.array[k];

                for (size_t j=0; j < k; j++) {
                    if (field.name == sd.fields.array[j].name) {
                        const SV msg = SV_from_str("field name must be unique");
                        syntax_error(&source_info->source, &source_info->source_filename, &field.raw_name, &msg);
                        exit(1);
                    }
                }

                const TC_typeid new_type = resolve_type(&field.type, &name_index, source_info, &materialized_generics, table, &out_structs, &out_enums);

                const TC_field new_field = (TC_field) {
                    field.name,
                    field.raw_name,
                    new_type
                };
                TC_field_LIST_push(&new_fields, new_field);
            }

            out_structs.array[n] = (TC_structdef) {
                sd.name,
                sd.raw_name,
                new_fields,
                (TC_struct_layout) { // TODO: CALC
                    0,
                    0,
                    NULL
                }
            };

        }
    }


    return (TC_TypeTable) {
        out_structs,
        out_enums,
        materialized_generics
    };

    // exit(255);
    //
    //
    //
    //
    // /// list of indexes to out_structs
    // /// remake into: list of incompleate names -> struct or enum type
    // // size_t_LIST incomplete_structs_indexes = size_t_LIST_new();
    //
    // /// uses uint64_t::MAX -> UINT64_MAX for deleted values
    // Identifier_LIST incomplete_type_names = Identifier_LIST_new();
    //
    // for (size_t n = 0; n < table->structs.len; n++) {
    //     const IR_structdef sd = table->structs.array[n];
    //     TC_field_LIST fields = TC_field_LIST_new();
    //     for (size_t f = 0; f < sd.fields.len; f++) {
    //         const IR_field field = sd.fields.array[f];
    //         const TC_typeid type = get_type(identifier_map, &out_structs, &out_enums, &incomplete_type_names, &field.type);
    //         TC_field_LIST_push(&fields, (TC_field){
    //                                .name = field.name,
    //                                .type = type
    //                            });
    //     }
    //     // check if an incomplete one with this name exists, if yes, replace it, if not push
    //     bool found = false;
    //     for (size_t k = 0; k < incomplete_type_names.len; k++) {
    //         const Identifier name = incomplete_type_names.array[k];
    //         if (name == sd.name) {
    //             found = true;
    //             incomplete_type_names.array[k] = UINT64_MAX;
    //
    //             bool found_struct = false;
    //             for (size_t j = 0; j < out_structs.len; j++) {
    //                 if (out_structs.array[j].name == name) {
    //                     out_structs.array[j].fields = fields;
    //                     out_structs.array[j].raw_name = sd.raw_name;
    //                     found_struct = true;
    //                     break;
    //                 }
    //             }
    //             if (!found_struct) {
    //                 const SV name_2 = SV_from_identifiermap_lookup(identifier_map, name);
    //                 fprintf(stderr, "[internal error] ctt-ff1 '"SV_format"'", SV_v_args(name_2));
    //                 exit(3);
    //             }
    //             break;
    //         }
    //     }
    //
    //
    //     // size_t remove_index = 0;
    //     // for (size_t k = 0; k < incomplete_structs_indexes.len; k++) {
    //     //     if (out_structs.array[incomplete_structs_indexes.array[k]].name == sd.name) {
    //     //         found = true;
    //     //         out_structs.array[incomplete_structs_indexes.array[k]].fields = fields;
    //     //         out_structs.array[incomplete_structs_indexes.array[k]].raw_name = sd.raw_name;
    //     //         remove_index = k;
    //     //         break;
    //     //     }
    //     // }
    //     if (found) {
    //         // // remove and restructure incomplete_structs_indexes
    //         // memmove(
    //         //     &incomplete_structs_indexes.array[remove_index],
    //         //     &incomplete_structs_indexes.array[remove_index + 1],
    //         //     (incomplete_structs_indexes.len - remove_index - 1) * sizeof(incomplete_structs_indexes.array[0])
    //         // );
    //         // incomplete_structs_indexes.len--;
    //     } else {
    //         TC_structdef_LIST_push(&out_structs, (TC_structdef){
    //             .name = sd.name,
    //             .raw_name = sd.raw_name,
    //             .fields = fields,
    //         });
    //     }
    // }
    //
    // // for (size_t n=0; n<incomplete_structs_indexes.len; n++) {
    // //     incomplete_structs_indexes.array[n]
    // // }
    // bool completeness_failed = false;
    // for (size_t n = 0; n < incomplete_type_names.len; n++) {
    //     if (incomplete_type_names.array[n] != UINT64_MAX) {
    //         const SV v = SV_from_identifiermap_lookup(identifier_map, incomplete_type_names.array[n]);
    //         fprintf(stderr, "incomplete type: '"SV_format"'", SV_v_args(v));
    //         completeness_failed = true;
    //     }
    // }
    // if (completeness_failed) {
    //     exit(1);
    // }
    // // if (incomplete_structs_indexes.len != 0) {
    // //     for (size_t n = 0; n < incomplete_structs_indexes.len; n++) {
    // //         const Identifier name = out_structs.array[incomplete_structs_indexes.array[n]].name;
    // //         const SV v = SV_from_identifiermap_lookup(identifier_map, name);
    // //         fprintf(stderr, "incomplete type: '"SV_format"'", SV_v_args(v));
    // //     }
    // //
    // //     exit(1);
    // // }
    //
    // TC_struct_layout* struct_layouts = malloc(sizeof(TC_struct_layout)*out_structs.len);
    //
    // {
    //     topo_state* state = calloc(out_structs.len, sizeof(topo_state));
    //     uint32_t* order   = malloc(out_structs.len * sizeof(uint32_t));
    //     uint32_t  order_len = 0;
    //
    //     for (uint32_t i = 0; i < out_structs.len; i++)
    //         dfs(i, &out_structs, state, order, &order_len, identifier_map, source_info);
    //
    //     // check for cycles before doing any layout
    //     for (uint32_t i = 0; i < out_structs.len; i++) {
    //         if (state[i] == ERROR) {
    //             fprintf(stderr, "cycle detected, use Box<T> if needed\n");
    //             // return;
    //             exit(1);
    //         }
    //     }
    //
    //     // TC_struct_layout* struct_layouts = malloc(sizeof(TC_struct_layout) * out_structs.len);
    //     for (uint32_t i = 0; i < order_len; i++)
    //         compute_layout(order[i], &out_structs, struct_layouts, target_info->ptr_size);
    //
    //     free(state);
    //     free(order);
    // }
    //
    // size_t_LIST_shrink(&enum_sizes);
    //
    // return (TC_TypeTable){
    //     out_structs,
    //     struct_layouts,
    //     out_enums,
    //     enum_sizes.array,
    //     materialized_generic_structs,
    //     materialized_generic_enums,
    // };
}

/// typechecking pass
void TC_typecheck(TargetInfo* restrict target_info, SVList* restrict tokens, const SV* restrict source, const SV* restrict source_filename, const TC_TypeTable* restrict tt, const IR_func_LIST* restrict code) {


}
