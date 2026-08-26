#include "typechecking.h"
#include "error.h"
#include "utils/dev.h"


void print_TC_typeid(const TC_typeid* restrict ty, khash_t(sv_u64_map)* restrict identifier_map, const TC_TypeTable* restrict tt) {
    switch (ty->ty) {
        case TC_TY_Primitive: {
            const SV k = TC_Primitive_TY_to_SV(ty->info.primitive);
            printf(SV_format, SV_v_args(k));
            break;
        }
        case TC_TY_Struct: {
            const SV name = SV_from_identifiermap_lookup(identifier_map, tt->structs.array[ty->info.type_id].name);
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
    fprintf(stderr, "[intern] invalid control path for TC_Primitive_TY sizeof | %u\n", (uint32_t)pt);
    exit(3);
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

TC_typeid get_type(const khash_t(sv_u64_map)* restrict identifier_map,
    TC_structdef_LIST* restrict structs,
    TC_enumdef_LIST* restrict enums,
    size_t_LIST* restrict incomplete_structs_indexes,
    const IR_typeuse* restrict type
) {
    switch (type->tt_ty) {
        case IR_TY_Primitive: {
            return (TC_typeid){
                .ty = TC_TY_Primitive,
                .info.primitive = type->info.primitive
            };
        }
        case IR_TY_Named: {
            // search existing ones if not found, make an incomplete one
            for (size_t n = 0; n < structs->len; n++) {
                if (type->info.name == structs->array[n].name) {
                    return (TC_typeid){
                        .ty = TC_TY_Struct,
                        .info.type_id = n
                    };
                }
            }
            TC_structdef_LIST_push(structs, (TC_structdef){.name = type->info.name});
            bool found = false;
            for (size_t n = 0; n < incomplete_structs_indexes->len; n++) {
                if (structs->array[incomplete_structs_indexes->array[n]].name == type->info.name) {
                    found = true;
                    break;
                }
            }
            const size_t id = structs->len - 1;
            if (!found) {
                size_t_LIST_push(incomplete_structs_indexes, id);
            }
            return (TC_typeid){
                .ty = TC_TY_Struct,
                .info.type_id = id,
            };
        }
        case IR_TY_Ref:
        case IR_TY_Array:
        case IR_TY_Box:
        case IR_TY_List: {
            TC_typeid* contained = malloc(sizeof(TC_typeid));
            *contained = get_type(identifier_map, structs, enums, incomplete_structs_indexes, type->contained);

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
                default: {
                    fprintf(stderr, "[internal] whut? what happened to control flow? | u402\n");
                    exit(3);
                }
            }

            return (TC_typeid){
                .ty = ty,
                .info.contained = contained
            };
        }
        case IR_TY_Generic: {
            // fprintf(stderr, "[WARNING] IR_Type to TC_type for GENERICS unimplemented\n");

            TC_typeid* contained = malloc(sizeof(TC_typeid)*type->contained_len);
            for (size_t n = 0; n < type->contained_len; n++) {
                const TC_typeid tc = get_type(identifier_map, structs, enums, incomplete_structs_indexes, &type->contained[n]);
                contained[n] = tc;
            }

            return (TC_typeid) {
                .ty = TC_TY_Generic,
                .info.generic = {
                    .contained = contained,
                    .contained_len = type->contained_len,
                    .generics_name = type->info.name,
                },
            };
        }
    }

    // return ();
    fprintf(stderr, "[internal] whut? what happened to control flow? | u403\n");
    exit(3);
}


// typedef enum { UNVISITED, IN_STACK, DONE } topo_state;
typedef enum { UNVISITED, IN_STACK, DONE, ERROR } topo_state;

void dfs(const uint32_t idx, const TC_structdef_LIST* restrict structs,
         topo_state* restrict state, uint32_t* restrict order, uint32_t* restrict order_len,
         const khash_t(sv_u64_map)* restrict identifier_map, const SourceInfo* restrict source_info)
{
    if (state[idx] == DONE || state[idx] == ERROR) return;
    if (state[idx] == IN_STACK) {
        // state[idx] = ERROR; return;
        const SV* name = &structs->array[idx].raw_name;
        const SV msg = SV_from_str("owned cycles are forbidden, use Ref<T> if you need to construct cycle in runtime");
        syntax_error(source_info->source, source_info->source_filename, name, &msg);
        // fprintf(stderr, "error: cyclic struct '"SV_format"'\n", SV_v_args(name));
        // abort();
        exit(1);
    }

    state[idx] = IN_STACK;
    const TC_structdef* s = &structs->array[idx];

    for (uint32_t i = 0; i < s->fields.len; i++) {
        const TC_typeid* ft = &s->fields.array[i].type;
        if (ft->ty == TC_TY_Struct) {
            dfs(ft->info.type_id, structs, state, order, order_len, identifier_map, source_info);
            if (state[ft->info.type_id] == ERROR) {
                state[idx] = ERROR; // propagate
                return;
            }
        }
    }

    state[idx] = DONE;
    order[(*order_len)++] = idx;
}

uint32_t size_of(const TC_typeid* restrict t, const TC_struct_layout* restrict layouts, const uint32_t ptr_size) {
    switch (t->ty) {
        case TC_TY_Primitive: return TC_Primitive_TY_size(t->info.primitive);
        case TC_TY_Struct:    return layouts[t->info.type_id].size;
        case TC_TY_Ref:       return ptr_size;
        case TC_TY_Array:     return ptr_size * 2;
        case TC_TY_List:      return ptr_size * 3;
        case TC_TY_Generic: {
            UNIMPLEMENTED("generics size")
            return -1;
        }
    }
    abort();
}

uint32_t align_of(const TC_typeid* restrict t, const TC_struct_layout* restrict layouts, const uint32_t ptr_size) {
    switch (t->ty) {
        case TC_TY_Primitive: {
            return TC_Primitive_TY_size(t->info.primitive); // holds for standard primitives
        }
        case TC_TY_Struct: {
            return layouts[t->info.type_id].align;
        }
        case TC_TY_Ref:
        case TC_TY_Array:
        case TC_TY_List: {
            return ptr_size;
        }
        case TC_TY_Generic:
            UNIMPLEMENTED("generics align")
            return -1;
    }
    abort();
}

void compute_layout(const uint32_t idx, const TC_structdef_LIST* restrict structs, TC_struct_layout* restrict layouts, const uint32_t ptr_size) {
    const TC_structdef* s = &structs->array[idx];
    TC_struct_layout* L = &layouts[idx];
    L->field_offsets = malloc(s->fields.len * sizeof(uint32_t));

    uint32_t offset = 0, max_align = 1;

    for (uint32_t i = 0; i < s->fields.len; i++) {
        const TC_typeid* ft = &s->fields.array[i].type;
        const uint32_t fa = align_of(ft, layouts, ptr_size);
        const uint32_t fs = size_of(ft, layouts, ptr_size);

        offset = (offset + fa - 1) & ~(fa - 1); // align up
        L->field_offsets[i] = offset;
        offset += fs;
        if (fa > max_align) max_align = fa;
    }

    L->align = max_align;
    L->size  = (offset + max_align - 1) & ~(max_align - 1); // tail padding
}

TC_TypeTable close_type_table(const IR_incomplete_TypeTable* restrict table,
                              const khash_t(sv_u64_map)* restrict identifier_map,
                              const TargetInfo* restrict target_info,
                              const SourceInfo* restrict source_info) {


    TC_structdef_LIST out_structs = TC_structdef_LIST_new();
    TC_enumdef_LIST out_enums = TC_enumdef_LIST_new();

    size_t_LIST incomplete_structs_indexes = size_t_LIST_new();

    for (size_t n = 0; n < table->structs.len; n++) {
        const IR_structdef sd = table->structs.array[n];
        TC_field_LIST fields = TC_field_LIST_new();
        for (size_t f = 0; f < sd.fields.len; f++) {
            const IR_field field = sd.fields.array[f];
            const TC_typeid type = get_type(identifier_map, &out_structs, &out_enums, &incomplete_structs_indexes, &field.type);
            TC_field_LIST_push(&fields, (TC_field){
                                   .name = field.name,
                                   .type = type
                               });
        }
        // check if a incomplete one with this name exists, if yes, replace it, if not push
        bool found = false;
        size_t remove_index = 0;
        for (size_t k = 0; k < incomplete_structs_indexes.len; k++) {
            if (out_structs.array[incomplete_structs_indexes.array[k]].name == sd.name) {
                found = true;
                out_structs.array[incomplete_structs_indexes.array[k]].fields = fields;
                out_structs.array[incomplete_structs_indexes.array[k]].raw_name = sd.raw_name;
                remove_index = k;
                break;
            }
        }
        if (found) {
            // remove and restructure incomplete_structs_indexes
            memmove(
                &incomplete_structs_indexes.array[remove_index],
                &incomplete_structs_indexes.array[remove_index + 1],
                (incomplete_structs_indexes.len - remove_index - 1) * sizeof(incomplete_structs_indexes.array[0])
            );
            incomplete_structs_indexes.len--;
        } else {
            TC_structdef_LIST_push(&out_structs, (TC_structdef){
                                       .name = sd.name,
                                        .raw_name = sd.raw_name,
                                       .fields = fields,
                                   });
        }
    }

    // for (size_t n=0; n<incomplete_structs_indexes.len; n++) {
    //     incomplete_structs_indexes.array[n]
    // }
    if (incomplete_structs_indexes.len != 0) {
        for (size_t n = 0; n < incomplete_structs_indexes.len; n++) {
            const Identifier name = out_structs.array[incomplete_structs_indexes.array[n]].name;
            const SV v = SV_from_identifiermap_lookup(identifier_map, name);
            fprintf(stderr, "incomplete type: '"SV_format"'", SV_v_args(v));
        }

        exit(1);
    }

    TC_struct_layout* struct_layouts = malloc(sizeof(TC_struct_layout)*out_structs.len);

    {
        topo_state* state = calloc(out_structs.len, sizeof(topo_state));
        uint32_t* order   = malloc(out_structs.len * sizeof(uint32_t));
        uint32_t  order_len = 0;

        for (uint32_t i = 0; i < out_structs.len; i++)
            dfs(i, &out_structs, state, order, &order_len, identifier_map, source_info);

        // check for cycles before doing any layout
        for (uint32_t i = 0; i < out_structs.len; i++) {
            if (state[i] == ERROR) {
                fprintf(stderr, "cycle detected, use Box<T> if needed\n");
                // return;
                exit(1);
            }
        }

        // TC_struct_layout* struct_layouts = malloc(sizeof(TC_struct_layout) * out_structs.len);
        for (uint32_t i = 0; i < order_len; i++)
            compute_layout(order[i], &out_structs, struct_layouts, target_info->ptr_size);

        free(state);
        free(order);
    }

    return (TC_TypeTable){
        .structs = out_structs,
        .struct_layouts = struct_layouts
    };
}

/// typechecking pass
void TC_typecheck(TargetInfo* restrict target_info, SVList* restrict tokens, const SV* restrict source, const SV* restrict source_filename, const TC_TypeTable* restrict tt, const IR_func_LIST* restrict code) {


}
