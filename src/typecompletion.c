#include "typecompletion.h"
#include "error.h"
#include "passes/parser/parser.h"
#include "utils/dev.h"


void print_TC_typeid(const TC_typeid* restrict ty, const khash_t(sv_u64_map)* restrict identifier_map, const TC_TypeTable* restrict nct) {
    switch (ty->ty) {
        case TC_TY_Void: {
            printf("Void");
            break;
        }
        case TC_TY_Primitive: {
            const SV k = TC_Primitive_TY_to_SV(ty->info.primitive);
            printf(SV_format, SV_v_args(k));
            break;
        }
        case TC_TY_Struct: {
            const SV name = nct->structs.array[ty->info.type_id].name.span.sv; // SV_from_identifiermap_lookup(identifier_map, nct->structs.array[ty->info.type_id].name);
            printf(SV_format"{%u}", SV_v_args(name), ty->info.type_id);
            break;
        }
        case TC_TY_Enum: {
            const SV name = nct->enums.array[ty->info.type_id].name.span.sv; // SV_from_identifiermap_lookup(identifier_map, nct->enums.array[ty->info.type_id].name);
            printf(SV_format"{%u}", SV_v_args(name), ty->info.type_id);
            break;
        }
        case TC_TY_Ref: {
            printf("Ref<");
            print_TC_typeid(ty->info.contained, identifier_map, nct);
            putc('>', stdout);
            break;
        }
        case TC_TY_Array: {
            printf("Array<");
            print_TC_typeid(ty->info.t_array.contained, identifier_map, nct);
            printf(", %u>", ty->info.t_array.array_size);
            break;
        }
        case TC_TY_List: {
            printf("List<");
            print_TC_typeid(ty->info.contained, identifier_map, nct);
            putc('>', stdout);
            break;
        }
        case TC_TY_Box: {
            printf("Box<");
            print_TC_typeid(ty->info.contained, identifier_map, nct);
            putc('>', stdout);
            break;
        }
        case TC_TY_Slice: {
            printf("Slice<");
            print_TC_typeid(ty->info.contained, identifier_map, nct);
            putc('>', stdout);
            break;
        }
        case TC_TY_External_Type: {
            printf("ExternalType{"SV_format"}", SV_v_args(ty->info.external_type.span.sv));
            break;
        }
    }
}

SV append_and_advance_buff(const SV* dest, const SV src) {
    // 1. Prevent buffer overflows by clamping the copy length to the remaining capacity
    const size_t copy_len = src.len < dest->len ? src.len : dest->len;

    // 2. Copy the data (casting away constness since SV often has a const char* start,
    //    but we are using it here as a writable buffer slice)
    if (copy_len > 0) {
        memcpy((char*)dest->start, src.start, copy_len);
    }

    // 3. Return a new SV shifted forward by the amount we just wrote
    return (SV){
        .start = dest->start + copy_len,
        .len = dest->len - copy_len
    };
}

SV format_TC_typeid_inner(SV buffer, const TC_typeid* restrict ty, const khash_t(sv_u64_map)* restrict identifier_map, const TC_TypeTable* restrict nct);

SV format_TC_typeid(const TC_typeid* restrict ty, const khash_t(sv_u64_map)* restrict identifier_map, const TC_TypeTable* restrict nct) {
    const size_t MAX_SIZE = 1024;
    char* buff = malloc(sizeof(char)*MAX_SIZE);
    const SV buffer = (SV) {.start = buff, .len = MAX_SIZE };
    const SV ret = format_TC_typeid_inner(buffer, ty, identifier_map, nct);
    char* new_buff = realloc(buff, ret.start - buffer.start);
    return (SV) { .start = new_buff, .len = ret.start - buffer.start};
}

#define MAKE_SV_LIT(X) (SV){.start=X, .len=sizeof(X)-1}

SV format_TC_typeid_inner(const SV buffer, const TC_typeid* restrict ty, const khash_t(sv_u64_map)* restrict identifier_map, const TC_TypeTable* restrict nct) {
    switch (ty->ty) {
        case TC_TY_Void: {
            return append_and_advance_buff(&buffer, MAKE_SV_LIT("Void"));
            break;
        }
        case TC_TY_Primitive: {
            const SV k = TC_Primitive_TY_to_SV(ty->info.primitive);
            return append_and_advance_buff(&buffer, k);
        }
        case TC_TY_Struct: {
            const SV name = nct->structs.array[ty->info.type_id].name.span.sv; // SV_from_identifiermap_lookup(identifier_map, nct->structs.array[ty->info.type_id].name);
            const SV b1 = append_and_advance_buff(&buffer, name);
            const int len = snprintf((char*)b1.start, b1.len, "{%u}", ty->info.type_id);
            const SV b2 = (SV){.start = b1.start + len, .len = b1.len - len };
            // printf(SV_format"{%u}", SV_v_args(name), ty->info.type_id);
            return b2;
            // break;
        }
        case TC_TY_Enum: {
            const SV name = nct->enums.array[ty->info.type_id].name.span.sv; // SV_from_identifiermap_lookup(identifier_map, nct->enums.array[ty->info.type_id].name);
            const SV b1 = append_and_advance_buff(&buffer, name);
            const int len = snprintf((char*)b1.start, b1.len, "{%u}", ty->info.type_id);
            const SV b2 = (SV){.start = b1.start + len, .len = b1.len - len };
            // printf(SV_format"{%u}", SV_v_args(name), ty->info.type_id);
            // break;
            return b2;
        }
        case TC_TY_Ref: {
            // printf("Ref<");
            const SV b1 = append_and_advance_buff(&buffer, MAKE_SV_LIT("Ref<"));
            const SV b2 = format_TC_typeid_inner(b1, ty->info.contained, identifier_map, nct);
            const SV b3 = append_and_advance_buff(&b2, MAKE_SV_LIT(">"));
            // putc('>', stdout);
            // break;
            return b3;
        }
        case TC_TY_Array: {
            // printf("Array<");
            const SV b1 = append_and_advance_buff(&buffer, MAKE_SV_LIT("Array<"));
            const SV b2 = format_TC_typeid_inner(b1, ty->info.t_array.contained, identifier_map, nct);
            const int len = snprintf((char*)b2.start, b2.len, ", %u>", ty->info.t_array.array_size);
            // printf(", %u>", ty->info.t_array.array_size);
            const SV b3 = (SV){.start = b2.start + len, .len = b2.len - len };;
            // break;
            return b3;
        }
        case TC_TY_List: {
            // printf("List<");
            const SV b1 = append_and_advance_buff(&buffer, MAKE_SV_LIT("List<"));
            const SV b2 = format_TC_typeid_inner(b1, ty->info.contained, identifier_map, nct);
            const SV b3 = append_and_advance_buff(&b2, MAKE_SV_LIT(">"));
            // putc('>', stdout);
            // break;
            return b3;
        }
        case TC_TY_Box: {
            // printf("Box<");
            const SV b1 = append_and_advance_buff(&buffer, MAKE_SV_LIT("Box<"));
            const SV b2 = format_TC_typeid_inner(b1, ty->info.contained, identifier_map, nct);
            // putc('>', stdout);
            const SV b3 = append_and_advance_buff(&b2, MAKE_SV_LIT(">"));
            // break;
            return b3;
        }
        case TC_TY_Slice: {
            // printf("Slice<");
            const SV b1 = append_and_advance_buff(&buffer, MAKE_SV_LIT("Slice<"));
            const SV b2 = format_TC_typeid_inner(b1, ty->info.contained, identifier_map, nct);
            // putc('>', stdout);
            const SV b3 = append_and_advance_buff(&b2, MAKE_SV_LIT(">"));
            // break;
            return b3;
        }
        case TC_TY_External_Type: {
            const SV b1 = append_and_advance_buff(&buffer, MAKE_SV_LIT("ExternalType{"));
            const SV b2 = append_and_advance_buff(&b1, ty->info.external_type.span.sv);
            const SV b3 = append_and_advance_buff(&b2, MAKE_SV_LIT("}"));
            // printf("ExternalType{"SV_format"}", SV_v_args(ty->info.external_type.span.sv));
            // break;
            return b3;
        }
    }
    UNREACHABLE()
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
    if (Token_cmp(sv, "Bool")) {
        *out = TC_Primitive__Bool;
        return true;
    }
    return false;
}

uint32_t TC_Primitive_TY_size(const TC_Primitive_TY pt) {
    switch (pt) {
        case TC_Primitive__Bool:
        case TC_Primitive__Char:
        case TC_Primitive__Int8:
        case TC_Primitive__Uint8: {
            return 1;
        }
        case TC_Primitive__Int16:
        case TC_Primitive__Uint16: {
            return 2;
        }
        case TC_Primitive__Int32:
        case TC_Primitive__Uint32: {
            return 4;
        }
        case TC_Primitive__Int64:
        case TC_Primitive__Uint64: {
            return 8;
        }
        case TC_Primitive__Float32: {
            return 4;
        }
        case TC_Primitive__Float64: {
            return 8;
        }
        // case TC_Primitive__Number:
        // case TC_Primitive__FloatNumber: {
        //     INTERNAL_ERROR()
        // }
    }
    UNREACHABLE()
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
        // case TC_Primitive__Number: {
        //     return SV_from_str_len("Number", 6);
        // }
        // case TC_Primitive__FloatNumber: {
        //     return SV_from_str_len("FloatNumber", 11);
        // }
        case TC_Primitive__Bool: {
            return SV_from_str_len("Bool", 4);
        }
    }
    UNREACHABLE()
}




/// returns TRUE if EQUAL
bool CMP_TC_typeid(const TC_typeid* restrict a, const TC_typeid* restrict b) {
    if (a->ty != b->ty) return false;
    switch (a->ty) {
        case TC_TY_Void: {
            return true;
        }
        case TC_TY_Primitive: {
            return a->info.primitive == b->info.primitive;
        }
        case TC_TY_Struct:
        case TC_TY_Enum: {
            return a->info.type_id == b->info.type_id;
        }
        case TC_TY_Ref:
        case TC_TY_List:
        case TC_TY_Slice:
        case TC_TY_Box: {
            return CMP_TC_typeid(a->info.contained, b->info.contained);
        }
        case TC_TY_Array: {
            return a->info.t_array.array_size == b->info.t_array.array_size && CMP_TC_typeid(a->info.t_array.contained, b->info.t_array.contained);
        }
        case TC_TY_External_Type: {
            return a->info.external_type.identifier == b->info.external_type.identifier;
        }
    }
    UNREACHABLE();
}


/// returns TRUE if a is equal b
bool CMP_IR_typeuse(const IR_typeuse* restrict a, const IR_typeuse* restrict b) {
    if (a->tt_ty != b->tt_ty) return false;
    switch (a->tt_ty) {
        case IR_TY_Primitive: {
            return a->info.primitive == b->info.primitive;
        }
        case IR_TY_Named: {
            return a->info.named.identifier == b->info.named.identifier;
        }
        case IR_TY_Ref:
        case IR_TY_Box:
        case IR_TY_Array:
        case IR_TY_Slice:
        case IR_TY_List: {
            return CMP_IR_typeuse(a->contained, b->contained);
        }
        case IR_TY_Generic: {
            // if (a->info.named.identifier != b->info.named.identifier) return false;
            // if (a->contained_len != b->contained_len) return false;
            // for(size_t n=0; n<a->contained_len; n++) {
            //     if (!CMP_IR_typeuse(&a->contained[n], &b->contained[n])) {
            //         return false;
            //     }
            // }
            // return true;

            if (a->info.named.identifier != b->info.named.identifier) {
                return false;
            }

            if (a->contained_len != b->contained_len) return false;

            for(size_t n=0; n<a->contained_len; n++) {
                if (!CMP_IR_typeuse(&a->contained[n], &b->contained[n])) {
                    return false;
                }
            }
            return true;
        }
        case IR_TY_Void: {
            return true;
        }
    }
    UNREACHABLE();
}

IR_typeuse substitute_typeuse(
    const IR_typeuse* restrict input,
    const SpannedIdentifier_LIST* restrict generic_args,
    const IR_typeuse* restrict args
) {
    switch (input->tt_ty) {
        case IR_TY_Void:
        case IR_TY_Primitive: {
            return *input;
        }
        case IR_TY_Named: {
            for (size_t g = 0; g < generic_args->len; g++) {
                if (input->info.named.identifier == generic_args->array[g].identifier) {
                    return args[g];
                }
            }

            return *input;
        }
        case IR_TY_Ref:
        case IR_TY_Box:
        case IR_TY_Array:
        case IR_TY_Slice:
        case IR_TY_List: {

            if (input->contained_len != 1) {
                INTERNAL_ERROR("invalid contained length of builtin type (%lu)", input->contained_len)
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
        }
    }
    UNREACHABLE()
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
        case IR_TY_Void: {
            return (TC_typeid) {
                .ty = TC_TY_Void,
                .full_typeuse_span = type->full_typeuse_span
            };
        }
        case IR_TY_Primitive: {
            return (TC_typeid){
                .ty = TC_TY_Primitive,
                .info.primitive = type->info.primitive,
                .full_typeuse_span = type->full_typeuse_span
            };
        }
        case IR_TY_Named: {
            for (size_t n=0; n<name_index->len; n++) {
                if (name_index->array[n].name == type->info.named.identifier) {
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
                        .full_typeuse_span = type->full_typeuse_span
                    };
                }
            }
            for (size_t n=0; n < table->extern_types.len; n++) {
                if (table->extern_types.array[n].identifier == type->info.named.identifier) {
                    return (TC_typeid) {
                        .ty = TC_TY_External_Type,
                        .info.external_type = type->info.named,
                        .full_typeuse_span = type->full_typeuse_span
                    };
                }
            }

            const SV msg = SV_from_str("undeclared type");
            syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.span.sv, &msg);
            exit(1);
        }
        case IR_TY_Array: {
            TC_typeid* contained = malloc(sizeof(TC_typeid));
            *contained = resolve_type(type->contained, name_index, source_info, materialized_generics, table, out_structs, out_enums);

            free(type->contained);

            TC_type_ty ty = TC_TY_Array;
            return (TC_typeid){
                .ty = ty,
                .info.t_array = {
                    contained,
                    type->array_const_size
                },
                .full_typeuse_span = type->full_typeuse_span
            };
        }
        case IR_TY_Ref:
        case IR_TY_Box:
        case IR_TY_Slice:
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
                case IR_TY_List: {
                    ty = TC_TY_List;
                    break;
                }
                case IR_TY_Box: {
                    ty = TC_TY_Box;
                    break;
                }
                case IR_TY_Slice: {
                    ty = TC_TY_Slice;
                    break;
                }
                default: {
                    UNREACHABLE();
                }
            }

            return (TC_typeid){
                .ty = ty,
                .info.contained = contained,
                .full_typeuse_span = type->full_typeuse_span
            };
        }
        case IR_TY_Generic: {
            // check if materialized:
            // materialized_generics
            for (size_t n=0; n<materialized_generics->len; n++) {
                const TC_materialized_generic_index_record mr = materialized_generics->array[n];
                if (mr.generic.tt_ty != IR_TY_Generic) {
                    INTERNAL_ERROR("should never happen | u403");
                }
                const IR_typeuse tu = mr.generic;
                if (tu.info.named.identifier != type->info.named.identifier) {
                    // ???
                    // TODO: should be ERROR
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
                    .full_typeuse_span = type->full_typeuse_span
                };
            }


            // MATERIALIZE ENUM
            for (size_t k=0; k<table->generic_enums.len; k++) {
                const IR_generic_enumdef ged = table->generic_enums.array[k];
                if (ged.name.identifier != type->info.named.identifier) continue;

                // is correct number of type args used?
                if (ged.generic_args.len != type->contained_len) {
                    char* raw_msg = calloc(1, 100);
                    snprintf(raw_msg, 100, "expected %lu type arguments, but found only %lu", ged.generic_args.len, type->contained_len);
                    const SV msg = SV_from_str(raw_msg);
                    syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.span.sv, &msg);
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
                        new_values,
                        variant.values_len
                    });
                }

                const TC_enumdef new_enumdef = (TC_enumdef) {
                    ged.name,
                    new_variants,
                    0,
                    0,
                    0
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
                    .full_typeuse_span = type->full_typeuse_span
                };
            }

            // MATERIALIZE STRUCT
            for (size_t k=0; k<table->generic_structs.len; k++) {
                const IR_generic_structdef ges = table->generic_structs.array[k];
                if (ges.name.identifier != type->info.named.identifier) continue;

                // is correct number of type args used?
                if (ges.generic_args.len != type->contained_len) {
                    char* raw_msg = calloc(1, 100);
                    snprintf(raw_msg, 100, "expected %lu type arguments, but found only %lu", ges.generic_args.len, type->contained_len);
                    const SV msg = SV_from_str(raw_msg);
                    syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.span.sv, &msg);
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
                        ty
                    };
                }

                const TC_structdef new_structdef = (TC_structdef) {
                    ges.name,
                    new_fields,
                    (TC_struct_layout) {
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
                    .full_typeuse_span = type->full_typeuse_span
                };
            }

            // by now, we know, it does not exist
            const SV msg = SV_from_str("unknow generic");
            syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.span.sv, &msg);
            exit(1);

        }
    }
    UNREACHABLE()
}



SizeAlign calc_type_size(
    const TC_typeid* restrict type,
    const TC_structdef_LIST* restrict out_structs,
    const TC_enumdef_LIST* restrict out_enums,

   size_t_LIST* restrict in_progress_structs,
   size_t_LIST* restrict in_progress_enums,

    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info
) {
    switch (type->ty) {
        case TC_TY_Void: {
            INTERNAL_ERROR("void type does not have size or align")
        }
        case TC_TY_External_Type: {
            const SV msg = SV_from_str("external type has unknow size and therefore cannot be used alone, use it with for example Ref<T>");
            syntax_error(&source_info->source, &source_info->source_filename, &type->info.external_type.span.sv, &msg);
            exit(1);
        }
        case TC_TY_Primitive: {
            return (SizeAlign) { TC_Primitive_TY_size(type->info.primitive), TC_Primitive_TY_size(type->info.primitive)};
        }
        case TC_TY_Slice: {
            return (SizeAlign) { target_info->ptr_size * 2, target_info->ptr_size }; // { ptr; len; }
        }
        case TC_TY_Array: {
            const SizeAlign inner = calc_type_size(type->info.t_array.contained, out_structs, out_enums, in_progress_structs, in_progress_enums, target_info, source_info);
            return (SizeAlign) {
                type->info.t_array.array_size * inner.size,
                inner.align
            };
        }
        case TC_TY_List: {
            return (SizeAlign) {target_info->ptr_size * 3, target_info->ptr_size }; // { ptr; len; cap; }
        }
        case TC_TY_Ref:
        case TC_TY_Box: {
            return (SizeAlign) {target_info->ptr_size, target_info->ptr_size };
        }
        case TC_TY_Struct: {
            TC_structdef* item = &out_structs->array[type->info.type_id];

            // already calculated
            if (item->layout.size != 0) {
                return (SizeAlign) { item->layout.size, item->layout.align };
            }

            for (size_t n=0; n < in_progress_structs->len; n++) {
                if (in_progress_structs->array[n] == type->info.type_id) { // cycle detected
                    const SV msg = SV_from_str("owned cycles are forbidden, use Box<T> if you need to construct cycle in runtime");
                    syntax_error(&source_info->source, &source_info->source_filename, &item->name.span.sv, &msg);
                    exit(1);
                }
            }

            // calc
            size_t_LIST_push(in_progress_structs, type->info.type_id); // mark as in_progress
            const size_t this_in_progress_nth = in_progress_structs->len-1;

            uint32_t* field_offsets = malloc(sizeof(uint32_t)*item->fields.len);


            uint32_t offset = 0;
            uint32_t max_align = 1;

            for (uint32_t i = 0; i < item->fields.len; i++) {
                const TC_typeid* ft = &item->fields.array[i].type;

                const SizeAlign sa = calc_type_size(ft, out_structs, out_enums, in_progress_structs, in_progress_enums, target_info, source_info);

                const uint32_t fa = sa.align;
                const uint32_t fs = sa.size;

                offset = (offset + fa - 1) & ~(fa - 1); // align up
                field_offsets[i] = offset;
                offset += fs;
                if (fa > max_align) max_align = fa;
            }

            const TC_struct_layout sl = (TC_struct_layout) {
                (offset + max_align - 1) & ~(max_align - 1), // tail padding,
                max_align,
                field_offsets
            };
            item->layout = sl; // write to the struct

            // done
            in_progress_structs->array[this_in_progress_nth] = SIZE_MAX;

            return (SizeAlign){ item->layout.size, item->layout.align };
        }
        case TC_TY_Enum: {
            TC_enumdef* item = &out_enums->array[type->info.type_id];

            if (item->size != 0) {
                return (SizeAlign){ item->size, item->align };
            }

            for (size_t n = 0; n < in_progress_enums->len; n++) {
                if (in_progress_enums->array[n] == type->info.type_id) {
                    const SV msg = SV_from_str("owned cycles are forbidden, use Box<T> if you need to construct cycle in runtime");
                    syntax_error(&source_info->source, &source_info->source_filename, &item->name.span.sv, &msg);
                    exit(1);
                }
            }

            size_t_LIST_push(in_progress_enums, type->info.type_id);
            const size_t this_in_progress_nth = in_progress_enums->len - 1;

            // discriminant is a primitive integer, align == size
            size_t disc_size;
            if      (item->variants.len <= 0xFF)       disc_size = 1;
            else if (item->variants.len <= 0xFFFF)     disc_size = 2;
            else if (item->variants.len <= 0xFFFFFFFF) disc_size = 4;
            else                                        disc_size = 8;

            size_t max_payload_size  = 0;
            size_t max_payload_align = 1;

            for (size_t v = 0; v < item->variants.len; v++) {
                const TC_enumdef_variant* variant = &item->variants.array[v];

                size_t payload_size  = 0;
                size_t variant_align = 1;

                for (size_t f = 0; f < variant->values_len; f++) {
                    const SizeAlign sa = calc_type_size(
                        &variant->values[f],
                        out_structs, out_enums,
                        in_progress_structs, in_progress_enums,
                        target_info, source_info
                    );
                    payload_size = (payload_size + sa.align - 1) & ~(sa.align - 1);
                    payload_size += sa.size;
                    if (sa.align > variant_align) variant_align = sa.align;
                }

                // round variant payload up to its own alignment
                payload_size = (payload_size + variant_align - 1) & ~(variant_align - 1);

                if (payload_size  > max_payload_size)  max_payload_size  = payload_size;
                if (variant_align > max_payload_align) max_payload_align = variant_align;
            }

            const size_t total_align   = disc_size > max_payload_align ? disc_size : max_payload_align;
            const size_t payload_start = (disc_size + max_payload_align - 1) & ~(max_payload_align - 1);
            const size_t raw_size      = payload_start + max_payload_size;
            const size_t total_size    = (raw_size + total_align - 1) & ~(total_align - 1);

            item->size  = total_size;
            item->align = total_align;
            item->discriminant_size = disc_size;

            in_progress_enums->array[this_in_progress_nth] = SIZE_MAX;

            return (SizeAlign){ total_size, total_align };
        }
    }
    UNREACHABLE()
}


TC_TypeTable close_type_table(const IR_incomplete_TypeTable* restrict table,
                              const khash_t(sv_u64_map)* restrict identifier_map,
                              const IR_generic_function_LIST generic_functions,
                              const TargetInfo* restrict target_info,
                              const SourceInfo* restrict source_info) {

    TC_name_index_record_LIST name_index = TC_name_index_record_LIST_new_with_capacity(table->enums.len + table->structs.len);

    for (size_t n=0; n<table->enums.len; n++) {
        const IR_enumdef ed = table->enums.array[n];
        for (size_t k=0; k<name_index.len; k++) {
            if (name_index.array[k].name == ed.name.identifier) {
                const SV msg = SV_from_str("type name collision");
                syntax_error(&source_info->source, &source_info->source_filename, &ed.name.span.sv, &msg);
                exit(1);
            }
        }
        TC_name_index_record_LIST_push(&name_index, (TC_name_index_record) {
            ed.name.identifier,
            TC_NIRK_Enum,
            n
        });
    }

    for (size_t n=0; n<table->structs.len; n++) {
        const IR_structdef sd = table->structs.array[n];
        for (size_t k=0; k<name_index.len; k++) {
            if (name_index.array[k].name == sd.name.identifier) {
                const SV msg = SV_from_str("type name collision");
                syntax_error(&source_info->source, &source_info->source_filename, &sd.name.span.sv, &msg);
                exit(1);
            }
        }
        TC_name_index_record_LIST_push(&name_index, (TC_name_index_record) {
            sd.name.identifier,
            TC_NIRK_Struct,
            n
        });
    }




    TC_structdef_LIST out_structs = TC_structdef_LIST_new_with_capacity(table->structs.len);
    out_structs.len = table->structs.len;

    TC_enumdef_LIST out_enums = TC_enumdef_LIST_new_with_capacity(table->enums.len);
    out_enums.len = table->enums.len;

    TC_materialized_generic_index_record_LIST materialized_generics = TC_materialized_generic_index_record_LIST_new();


    {
        for (size_t n=0; n<table->enums.len; n++) {

            const IR_enumdef ed = table->enums.array[n];
            TC_enumdef_variant_LIST new_variants = TC_enumdef_variant_LIST_new_with_capacity(ed.variants.len);

            for (size_t k=0; k< ed.variants.len; k++) {
                const IR_enumdef_variant variant = ed.variants.array[k];

                for (size_t j=0; j < k; j++) {
                    if (variant.name.identifier == ed.variants.array[j].name.identifier) {
                        const SV msg = SV_from_str("variant name must be unique");
                        syntax_error(&source_info->source, &source_info->source_filename, &variant.name.span.sv, &msg);
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
                    new_values,
                    variant.values_len
                };
                TC_enumdef_variant_LIST_push(&new_variants, new_variant);
            }

            out_enums.array[n] = (TC_enumdef) {
                ed.name,
                new_variants,
                0,
                0,
                0
            };
        }

        for (size_t n=0; n<table->structs.len; n++) {
            const IR_structdef sd = table->structs.array[n];
            // sd.fields
            TC_field_LIST new_fields = TC_field_LIST_new_with_capacity(sd.fields.len);
            for (size_t k=0; k<sd.fields.len; k++) {
                const IR_field field = sd.fields.array[k];

                for (size_t j=0; j < k; j++) {
                    if (field.name.identifier == sd.fields.array[j].name.identifier) {
                        const SV msg = SV_from_str("field name must be unique");
                        syntax_error(&source_info->source, &source_info->source_filename, &field.name.span.sv, &msg);
                        exit(1);
                    }
                }

                const TC_typeid new_type = resolve_type(&field.type, &name_index, source_info, &materialized_generics, table, &out_structs, &out_enums);

                const TC_field new_field = (TC_field) {
                    field.name,
                    new_type
                };
                TC_field_LIST_push(&new_fields, new_field);
            }

            out_structs.array[n] = (TC_structdef) {
                sd.name,
                new_fields,
                (TC_struct_layout) {
                    0,
                    0,
                    NULL
                }
            };

        }
    }

    // either go through all types and calc size, or fix sizes in the materialization
    size_t_LIST in_progress_structs = size_t_LIST_new();
    size_t_LIST in_progress_enums = size_t_LIST_new();

    for (size_t n=0; n < out_structs.len; n++) {
        calc_type_size(&(TC_typeid) {
            .ty = TC_TY_Struct,
            .info.type_id = n,
        }, &out_structs, &out_enums, &in_progress_structs, &in_progress_enums, target_info, source_info);

        in_progress_structs.len = 0;
        in_progress_enums.len = 0;
    }

    for (size_t n=0; n < out_enums.len; n++) {
        calc_type_size(&(TC_typeid) {
            .ty = TC_TY_Enum,
            .info.type_id = n
        }, &out_structs, &out_enums, &in_progress_structs, &in_progress_enums, target_info, source_info);

        in_progress_structs.len = 0;
        in_progress_enums.len = 0;
    }


    return (TC_TypeTable) {
        out_structs,
        out_enums,
        name_index,
        materialized_generics,
        table->generic_structs,
        table->generic_enums,
        generic_functions,
        IR_materialized_generic_function_LIST_new(),
        table->extern_types
    };
}


