
#include "type_resolve.h"
#include "passes/inference_check_types.h"
#include "passes/type_utils.h"
#include "error.h"
#include "utils/color.h"


// todo: fix for;
/*
generic struct Node<T> {
    T value;
    Option<Node<T>> next;
}
 */
TC_typeid resolve_tc_type2_inner(
    const IR_typeuse* restrict type,
    TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info,
    TC_incomplete_LIST* restrict incomplete_list
) {
    switch (type->tt_ty) {
        case IR_TY_Void: {
            return (TC_typeid){
                .ty = TC_TY_Void,
                // .full_typeuse_span = type->full_typeuse_span
            };
        }
        case IR_TY_Primitive: {
            return (TC_typeid){
                .ty = TC_TY_Primitive,
                .info.primitive = type->info.primitive,
                // .full_typeuse_span = type->full_typeuse_span
            };
        }
        case IR_TY_Named: {
            for (size_t n = 0; n < nct->enums.len; n++) {
                if (nct->enums.array[n].name.identifier == type->info.named.identifier) {
                    return (TC_typeid){
                        .ty = TC_TY_Enum,
                        .info.type_id = n,
                        // .full_typeuse_span = type->full_typeuse_span
                    };
                }
            }
            for (size_t n = 0; n < nct->structs.len; n++) {
                if (nct->structs.array[n].name.identifier == type->info.named.identifier) {
                    return (TC_typeid){
                        .ty = TC_TY_Struct,
                        .info.type_id = n,
                        // .full_typeuse_span = type->full_typeuse_span
                    };
                }
            }
            for (size_t n = 0; n < nct->external_types.len; n++) {
                if (nct->external_types.array[n].identifier == type->info.named.identifier) {
                    return (TC_typeid){
                        .ty = TC_TY_External_Type,
                        .info.external_type = type->info.named,
                        // .full_typeuse_span = type->full_typeuse_span
                    };
                }
            }

            const SV msg = SV_from_str_len("unknow type", 11);
            syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.span.sv, &msg);
            if (SV_pv_cmp_eq(&type->info.named.span.sv, "String", 6)) {
                const SV msg2 = SV_from_str(
                    "String as type does not exist, you can use "C_BOLD"Slice<Char>"C_RESET", "C_BOLD"List<Char>"C_RESET
                    ", "C_BOLD"Array<Char, N>"C_RESET" instead");
                hint_standalone(&msg2);
            }
            exit(1);
        }
        case IR_TY_Array: {
            TC_typeid* contained = malloc(sizeof(TC_typeid));
            *contained = resolve_tc_type2_inner(type->contained, nct, target_info, source_info, incomplete_list);
            TC_type_ty ty = TC_TY_Array;
            return (TC_typeid){
                .ty = ty,
                .info.t_array = {contained, type->array_const_size},
                // .full_typeuse_span = type->full_typeuse_span
            };
            break;
        }
        case IR_TY_Ref:
        case IR_TY_Box:
        case IR_TY_Slice:
        case IR_TY_List: {
            TC_typeid* contained = malloc(sizeof(TC_typeid));
            *contained = resolve_tc_type2_inner(type->contained, nct, target_info, source_info, incomplete_list);

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
                // .full_typeuse_span = type->full_typeuse_span
            };
        }
        case IR_TY_Generic: {
            TC_index_record_kind materialize_kind;
            size_t materialize_nth;

            for (size_t n = 0; n < nct->materialized_generics.len; n++) {
                const TC_materialized_generic_index_record mr = nct->materialized_generics.array[n];
                if (mr.generic.tt_ty != IR_TY_Generic) {
                    INTERNAL_ERROR("should never happen | u403");
                }

                const IR_typeuse tu = mr.generic;
                if (tu.info.named.identifier != type->info.named.identifier) {
                    continue;
                }

                if (tu.contained_len != type->contained_len) {
                    char* r_msg = malloc(sizeof(char) * 100);
                    snprintf(r_msg, 100, "for generic expected %lu args, but got %lu", tu.contained_len,
                             type->contained_len);
                    const SV msg = SV_from_str(r_msg);
                    syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.span.sv, &msg);
                    free(r_msg);
                    exit(1);
                }

                // bool args_match = false;
                bool arguments_are_identical = true;
                for (size_t j = 0; j < tu.contained_len; j++) {
                    // if (!CMP_IR_typeuse(&tu.contained[j], &type->contained[j])) {
                    //     args_match = true;
                    //     break;
                    // }
                    // 1. Resolve BOTH arguments into their true semantic types
                    TC_typeid cached_arg = resolve_tc_type2_inner(&tu.contained[j], nct, target_info, source_info, incomplete_list);
                    TC_typeid new_arg = resolve_tc_type2_inner(&type->contained[j], nct, target_info, source_info, incomplete_list);

                    // 2. Compare the semantic types!
                    // This perfectly bridges the gap if one was parsed as Named and one as Primitive.
                    if (!is_type_eq(&cached_arg, &new_arg)) {
                        arguments_are_identical = false;
                        break;
                    }
                }
                // if (args_match) {
                // continue;
                // }
                if (!arguments_are_identical) {
                    continue; // Mismatch! Move on to check the next generic in the cache.
                }

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
                return (TC_typeid){
                    .ty = ty,
                    .info.type_id = mr.nth,
                    // .full_typeuse_span = type->full_typeuse_span
                };
            }

            for (size_t n = 0; n < nct->generic_enums.len; n++) {
                if (nct->generic_enums.array[n].name.identifier == type->info.named.identifier) {
                    if (nct->generic_enums.array[n].generic_args.len != type->contained_len) {
                        char* r_msg = malloc(sizeof(char) * 100);
                        snprintf(r_msg, 100, "for generic expected %lu args, but got %lu",
                                 nct->generic_enums.array[n].generic_args.len, type->contained_len);
                        const SV msg = SV_from_str(r_msg);
                        syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.span.sv,
                                     &msg);
                        free(r_msg);
                        exit(1);
                    }
                    materialize_kind = TC_NIRK_Enum;
                    materialize_nth = n;
                    goto materialize;
                }
            }

            for (size_t n = 0; n < nct->generic_structs.len; n++) {
                if (nct->generic_structs.array[n].name.identifier == type->info.named.identifier) {
                    if (nct->generic_structs.array[n].generic_args.len != type->contained_len) {
                        char* r_msg = malloc(sizeof(char) * 100);
                        snprintf(r_msg, 100, "for generic expected %lu args, but got %lu",
                                 nct->generic_structs.array[n].generic_args.len, type->contained_len);
                        const SV msg = SV_from_str(r_msg);
                        syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.span.sv,
                                     &msg);
                        free(r_msg);
                        exit(1);
                    }
                    materialize_kind = TC_NIRK_Struct;
                    materialize_nth = n;
                    goto materialize;
                }
            }

            {
                const SV msg = SV_from_str("unknow generic");
                syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.span.sv, &msg);
                exit(1);
            }

        // materialize
        materialize: {
                switch (materialize_kind) {
                    case TC_NIRK_Enum: {
                        IR_generic_enumdef ged = nct->generic_enums.array[materialize_nth];

                        TC_enumdef_variant_LIST new_variants = TC_enumdef_variant_LIST_new_with_capacity(ged.variants.len);
                        new_variants.len = ged.variants.len;

                        const TC_enumdef new_enumdef = (TC_enumdef){
                            ged.name,
                            new_variants,
                            0,
                            0,
                            0,
                            true,
                            nct->materialized_generics.len
                        };


                        const size_t nth = nct->enums.len;
                        TC_enumdef_LIST_push(&nct->enums, new_enumdef);
                        TC_materialized_generic_index_record_LIST_push(&nct->materialized_generics,
                                                                       (TC_materialized_generic_index_record){
                                                                           *type,
                                                                           TC_NIRK_Enum,
                                                                           nth
                                                                       });

                        TC_incomplete_LIST_push(incomplete_list, (TC_incomplete) {
                            nth,
                            TC_NIRK_Enum
                        });




                        // materialize
                        for (size_t j = 0; j < ged.variants.len; j++) {
                            const IR_enumdef_variant variant = ged.variants.array[j];
                            TC_typeid* new_values = malloc(sizeof(TC_typeid) * variant.values_len);

                            for (size_t v = 0; v < variant.values_len; v++) {
                                const IR_typeuse substituted = substitute_typeuse(
                                    &variant.values[v], &ged.generic_args, type->contained);
                                const TC_typeid ty = resolve_tc_type2_inner(&substituted, nct, target_info, source_info, incomplete_list);
                                new_values[v] = ty;
                            }

                            new_variants.array[j] = (TC_enumdef_variant){
                                variant.name,
                                new_values,
                                variant.values_len
                            };
                            // TC_enumdef_variant_LIST_push(&new_variants, (TC_enumdef_variant){
                            //                                  variant.name,
                            //                                  new_values,
                            //                                  variant.values_len
                            //                              });
                        }



                        const TC_typeid ret = (TC_typeid){
                            .ty = TC_TY_Enum,
                            .info.type_id = nth,
                            // .full_typeuse_span = type->full_typeuse_span
                        };

                        // {
                        //     size_t_LIST in_progress_structs = size_t_LIST_new();
                        //     size_t_LIST in_progress_enums = size_t_LIST_new();
                        //
                        //     path_size_align_for_materialized_type(&ret, nct, &in_progress_structs, &in_progress_enums,
                        //                                           target_info, source_info);
                        //
                        //     size_t_LIST_free(&in_progress_structs);
                        //     size_t_LIST_free(&in_progress_enums);
                        // }

                        return ret;
                    }
                    case TC_NIRK_Struct: {
                        IR_generic_structdef ges = nct->generic_structs.array[materialize_nth];


                        TC_field_LIST new_fields = TC_field_LIST_new_with_capacity(ges.fields.len);
                        new_fields.len = ges.fields.len;

                        const TC_structdef new_structdef = (TC_structdef){
                            ges.name,
                            new_fields,
                            (TC_struct_layout){
                                0,
                                0,
                                NULL
                            },
                            true,
                            nct->materialized_generics.len
                        };

                        const size_t nth = nct->structs.len;
                        TC_structdef_LIST_push(&nct->structs, new_structdef);
                        TC_materialized_generic_index_record_LIST_push(&nct->materialized_generics,
                                                                       (TC_materialized_generic_index_record){
                                                                           *type,
                                                                           TC_NIRK_Struct,
                                                                           nth
                                                                       });

                        TC_incomplete_LIST_push(incomplete_list, (TC_incomplete) {
                            nth,
                            TC_NIRK_Struct
                        });

                        for (size_t j = 0; j < ges.fields.len; j++) {
                            const IR_field fd = ges.fields.array[j];
                            const IR_typeuse substituted = substitute_typeuse(
                                &fd.type, &ges.generic_args, type->contained);
                            const TC_typeid ty = resolve_tc_type2_inner(&substituted, nct, target_info, source_info, incomplete_list);
                            new_fields.array[j] = (TC_field){
                                fd.name,
                                ty
                            };
                        }

                        // const TC_structdef new_structdef = (TC_structdef){
                        //     ges.name,
                        //     new_fields,
                        //     (TC_struct_layout){
                        //         0,
                        //         0,
                        //         NULL
                        //     },
                        //     true,
                        //     nct->materialized_generics.len
                        // };
                        //
                        // const size_t nth = nct->structs.len;
                        // TC_structdef_LIST_push(&nct->structs, new_structdef);
                        // TC_materialized_generic_index_record_LIST_push(&nct->materialized_generics,
                        //                                                (TC_materialized_generic_index_record){
                        //                                                    *type,
                        //                                                    TC_NIRK_Struct,
                        //                                                    nth
                        //                                                });


                        const TC_typeid ret = (TC_typeid){
                            .ty = TC_TY_Struct,
                            .info.type_id = nth,
                            // .full_typeuse_span = type->full_typeuse_span
                        };

                        // {
                        //     size_t_LIST in_progress_structs = size_t_LIST_new();
                        //     size_t_LIST in_progress_enums = size_t_LIST_new();
                        //
                        //     path_size_align_for_materialized_type(&ret, nct, &in_progress_structs, &in_progress_enums,
                        //                                           target_info, source_info);
                        //
                        //     size_t_LIST_free(&in_progress_structs);
                        //     size_t_LIST_free(&in_progress_enums);
                        // }


                        return ret;
                    }
                }
            }

            UNREACHABLE()
        }
    }
    UNREACHABLE()
}



TC_typeid resolve_tc_type(
    const IR_typeuse* restrict type,
    TC_TypeTable* restrict nct,
    const TargetInfo* restrict target_info,
    const SourceInfo* restrict source_info,
    const khash_t(sv_u64_map)* restrict identifier_map
) {

    TC_incomplete_LIST incomplete_list = TC_incomplete_LIST_new();
    const TC_typeid out = resolve_tc_type2_inner(type, nct, target_info, source_info, &incomplete_list);

    // size_t_LIST in_progress_structs = size_t_LIST_new();
    // size_t_LIST in_progress_enums = size_t_LIST_new();
    TC_layout_dependency_LIST in_progress = TC_layout_dependency_LIST_new();

    // todo: revisit the resolution and calc size for any incomplete type
    for (size_t n = 0; n < incomplete_list.len; n++) {
        const TC_typeid t = (TC_typeid) {
            incomplete_list.array[n].kind == TC_NIRK_Struct ? TC_TY_Struct : TC_TY_Enum,
            .info.type_id = incomplete_list.array[n].index,
        };

        // path_size_align_for_materialized_type(&t, nct, &in_progress, target_info, source_info);
        calc_type_size(&t, nct, &in_progress, target_info, source_info, identifier_map);

        if (in_progress.len > 0) {
            INTERNAL_ERROR()
        }
    }
    // size_t_LIST_free(&in_progress_structs);
    // size_t_LIST_free(&in_progress_enums);
    TC_layout_dependency_LIST_free(&in_progress);

    TC_incomplete_LIST_free(&incomplete_list);

    return out;
}



// TC_typeid resolve_tc_type2(
//     const IR_typeuse* restrict type,
//     TC_TypeTable* restrict nct,
//     const TargetInfo* restrict target_info,
//     const SourceInfo* restrict source_info
// ) {
//     switch (type->tt_ty) {
//         case IR_TY_Void: {
//             return (TC_typeid){
//                 .ty = TC_TY_Void,
//                 .full_typeuse_span = type->full_typeuse_span
//             };
//         }
//         case IR_TY_Primitive: {
//             return (TC_typeid){
//                 .ty = TC_TY_Primitive,
//                 .info.primitive = type->info.primitive,
//                 .full_typeuse_span = type->full_typeuse_span
//             };
//         }
//         case IR_TY_Named: {
//             for (size_t n = 0; n < nct->enums.len; n++) {
//                 if (nct->enums.array[n].name.identifier == type->info.named.identifier) {
//                     return (TC_typeid){
//                         .ty = TC_TY_Enum,
//                         .info.type_id = n,
//                         .full_typeuse_span = type->full_typeuse_span
//                     };
//                 }
//             }
//             for (size_t n = 0; n < nct->structs.len; n++) {
//                 if (nct->structs.array[n].name.identifier == type->info.named.identifier) {
//                     return (TC_typeid){
//                         .ty = TC_TY_Struct,
//                         .info.type_id = n,
//                         .full_typeuse_span = type->full_typeuse_span
//                     };
//                 }
//             }
//             for (size_t n = 0; n < nct->external_types.len; n++) {
//                 if (nct->external_types.array[n].identifier == type->info.named.identifier) {
//                     return (TC_typeid){
//                         .ty = TC_TY_External_Type,
//                         .info.external_type = type->info.named,
//                         .full_typeuse_span = type->full_typeuse_span
//                     };
//                 }
//             }
//
//             const SV msg = SV_from_str_len("unknow type", 11);
//             syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.span.sv, &msg);
//             if (SV_pv_cmp_eq(&type->info.named.span.sv, "String", 6)) {
//                 const SV msg2 = SV_from_str(
//                     "String as type does not exist, you can use "C_BOLD"Slice<Char>"C_RESET", "C_BOLD"List<Char>"C_RESET
//                     ", "C_BOLD"Array<Char, N>"C_RESET" instead");
//                 hint_standalone(&msg2);
//             }
//             exit(1);
//         }
//         case IR_TY_Array: {
//             TC_typeid* contained = malloc(sizeof(TC_typeid));
//             *contained = resolve_tc_type(type->contained, nct, target_info, source_info);
//             TC_type_ty ty = TC_TY_Array;
//             return (TC_typeid){
//                 .ty = ty,
//                 .info.t_array = {contained, type->array_const_size},
//                 .full_typeuse_span = type->full_typeuse_span
//             };
//             break;
//         }
//         case IR_TY_Ref:
//         case IR_TY_Box:
//         case IR_TY_Slice:
//         case IR_TY_List: {
//             TC_typeid* contained = malloc(sizeof(TC_typeid));
//             *contained = resolve_tc_type(type->contained, nct, target_info, source_info);
//
//             TC_type_ty ty;
//             switch (type->tt_ty) {
//                 case IR_TY_Ref: {
//                     ty = TC_TY_Ref;
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
//                 case IR_TY_Slice: {
//                     ty = TC_TY_Slice;
//                     break;
//                 }
//                 default: {
//                     UNREACHABLE();
//                 }
//             }
//
//             return (TC_typeid){
//                 .ty = ty,
//                 .info.contained = contained,
//                 .full_typeuse_span = type->full_typeuse_span
//             };
//         }
//         case IR_TY_Generic: {
//             TC_index_record_kind materialize_kind;
//             size_t materialize_nth;
//
//
//             for (size_t n = 0; n < nct->materialized_generics.len; n++) {
//                 const TC_materialized_generic_index_record mr = nct->materialized_generics.array[n];
//                 if (mr.generic.tt_ty != IR_TY_Generic) {
//                     INTERNAL_ERROR("should never happen | u403");
//                 }
//
//                 const IR_typeuse tu = mr.generic;
//                 if (tu.info.named.identifier != type->info.named.identifier) {
//                     continue;
//                 }
//
//                 if (tu.contained_len != type->contained_len) {
//                     char* r_msg = malloc(sizeof(char) * 100);
//                     snprintf(r_msg, 100, "for generic expected %lu args, but got %lu", tu.contained_len,
//                              type->contained_len);
//                     const SV msg = SV_from_str(r_msg);
//                     syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.span.sv, &msg);
//                     free(r_msg);
//                     exit(1);
//                 }
//
//                 // bool args_match = false;
//                 bool arguments_are_identical = true;
//                 for (size_t j = 0; j < tu.contained_len; j++) {
//                     // if (!CMP_IR_typeuse(&tu.contained[j], &type->contained[j])) {
//                     //     args_match = true;
//                     //     break;
//                     // }
//                     // 1. Resolve BOTH arguments into their true semantic types
//                     TC_typeid cached_arg = resolve_tc_type(&tu.contained[j], nct, target_info, source_info);
//                     TC_typeid new_arg = resolve_tc_type(&type->contained[j], nct, target_info, source_info);
//
//                     // 2. Compare the semantic types!
//                     // This perfectly bridges the gap if one was parsed as Named and one as Primitive.
//                     if (!is_type_eq(&cached_arg, &new_arg)) {
//                         arguments_are_identical = false;
//                         break;
//                     }
//                 }
//                 // if (args_match) {
//                 // continue;
//                 // }
//                 if (!arguments_are_identical) {
//                     continue; // Mismatch! Move on to check the next generic in the cache.
//                 }
//
//                 TC_type_ty ty = TC_TY_Struct;
//                 switch (mr.kind) {
//                     case TC_NIRK_Enum: {
//                         ty = TC_TY_Enum;
//                         break;
//                     }
//                     case TC_NIRK_Struct: {
//                         ty = TC_TY_Struct;
//                         break;
//                     }
//                 }
//                 return (TC_typeid){
//                     .ty = ty,
//                     .info.type_id = mr.nth,
//                     .full_typeuse_span = type->full_typeuse_span
//                 };
//             }
//
//             for (size_t n = 0; n < nct->generic_enums.len; n++) {
//                 if (nct->generic_enums.array[n].name.identifier == type->info.named.identifier) {
//                     if (nct->generic_enums.array[n].generic_args.len != type->contained_len) {
//                         char* r_msg = malloc(sizeof(char) * 100);
//                         snprintf(r_msg, 100, "for generic expected %lu args, but got %lu",
//                                  nct->generic_enums.array[n].generic_args.len, type->contained_len);
//                         const SV msg = SV_from_str(r_msg);
//                         syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.span.sv,
//                                      &msg);
//                         free(r_msg);
//                         exit(1);
//                     }
//                     materialize_kind = TC_NIRK_Enum;
//                     materialize_nth = n;
//                     goto materialize;
//                 }
//             }
//
//             for (size_t n = 0; n < nct->generic_structs.len; n++) {
//                 if (nct->generic_structs.array[n].name.identifier == type->info.named.identifier) {
//                     if (nct->generic_structs.array[n].generic_args.len != type->contained_len) {
//                         char* r_msg = malloc(sizeof(char) * 100);
//                         snprintf(r_msg, 100, "for generic expected %lu args, but got %lu",
//                                  nct->generic_structs.array[n].generic_args.len, type->contained_len);
//                         const SV msg = SV_from_str(r_msg);
//                         syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.span.sv,
//                                      &msg);
//                         free(r_msg);
//                         exit(1);
//                     }
//                     materialize_kind = TC_NIRK_Struct;
//                     materialize_nth = n;
//                     goto materialize;
//                 }
//             }
//
//             {
//                 const SV msg = SV_from_str("unknow generic");
//                 syntax_error(&source_info->source, &source_info->source_filename, &type->info.named.span.sv, &msg);
//                 exit(1);
//             }
//
//         // materialize
//         materialize: {
//                 switch (materialize_kind) {
//                     case TC_NIRK_Enum: {
//                         IR_generic_enumdef ged = nct->generic_enums.array[materialize_nth];
//
//                         TC_enumdef_variant_LIST new_variants = TC_enumdef_variant_LIST_new_with_capacity(ged.variants.len);
//
//                         const TC_enumdef new_enumdef = (TC_enumdef){
//                             ged.name,
//                             new_variants,
//                             0,
//                             0,
//                             0,
//                             true,
//                             nct->materialized_generics.len
//                         };
//
//
//                         const size_t nth = nct->enums.len;
//                         TC_enumdef_LIST_push(&nct->enums, new_enumdef);
//                         TC_materialized_generic_index_record_LIST_push(&nct->materialized_generics,
//                                                                        (TC_materialized_generic_index_record){
//                                                                            *type,
//                                                                            TC_NIRK_Enum,
//                                                                            nth
//                                                                        });
//
//
//
//
//
//                         // materialize
//                         for (size_t j = 0; j < ged.variants.len; j++) {
//                             const IR_enumdef_variant variant = ged.variants.array[j];
//                             TC_typeid* new_values = malloc(sizeof(TC_typeid) * variant.values_len);
//
//                             for (size_t v = 0; v < variant.values_len; v++) {
//                                 const IR_typeuse substituted = substitute_typeuse(
//                                     &variant.values[v], &ged.generic_args, type->contained);
//                                 const TC_typeid ty = resolve_tc_type(&substituted, nct, target_info, source_info);
//                                 new_values[v] = ty;
//                             }
//
//                             TC_enumdef_variant_LIST_push(&new_variants, (TC_enumdef_variant){
//                                                              variant.name,
//                                                              new_values,
//                                                              variant.values_len
//                                                          });
//                         }
//
//
//
//                         const TC_typeid ret = (TC_typeid){
//                             .ty = TC_TY_Enum,
//                             .info.type_id = nth,
//                             .full_typeuse_span = type->full_typeuse_span
//                         };
//
//                         {
//                             size_t_LIST in_progress_structs = size_t_LIST_new();
//                             size_t_LIST in_progress_enums = size_t_LIST_new();
//
//                             // path_size_align_for_materialized_type(&ret, nct, &in_progress_structs, &in_progress_enums,
//                             // target_info, source_info);
//
//                             size_t_LIST_free(&in_progress_structs);
//                             size_t_LIST_free(&in_progress_enums);
//                         }
//
//                         return ret;
//                     }
//                     case TC_NIRK_Struct: {
//                         IR_generic_structdef ges = nct->generic_structs.array[materialize_nth];
//
//
//                         TC_field_LIST new_fields = TC_field_LIST_new_with_capacity(ges.fields.len);
//                         new_fields.len = ges.fields.len;
//
//                         const TC_structdef new_structdef = (TC_structdef){
//                             ges.name,
//                             new_fields,
//                             (TC_struct_layout){
//                                 0,
//                                 0,
//                                 NULL
//                             },
//                             true,
//                             nct->materialized_generics.len
//                         };
//
//                         const size_t nth = nct->structs.len;
//                         TC_structdef_LIST_push(&nct->structs, new_structdef);
//                         TC_materialized_generic_index_record_LIST_push(&nct->materialized_generics,
//                                                                        (TC_materialized_generic_index_record){
//                                                                            *type,
//                                                                            TC_NIRK_Struct,
//                                                                            nth
//                                                                        });
//
//                         for (size_t j = 0; j < ges.fields.len; j++) {
//                             const IR_field fd = ges.fields.array[j];
//                             const IR_typeuse substituted = substitute_typeuse(
//                                 &fd.type, &ges.generic_args, type->contained);
//                             const TC_typeid ty = resolve_tc_type(&substituted, nct, target_info, source_info);
//                             new_fields.array[j] = (TC_field){
//                                 fd.name,
//                                 ty
//                             };
//                         }
//
//                         // const TC_structdef new_structdef = (TC_structdef){
//                         //     ges.name,
//                         //     new_fields,
//                         //     (TC_struct_layout){
//                         //         0,
//                         //         0,
//                         //         NULL
//                         //     },
//                         //     true,
//                         //     nct->materialized_generics.len
//                         // };
//                         //
//                         // const size_t nth = nct->structs.len;
//                         // TC_structdef_LIST_push(&nct->structs, new_structdef);
//                         // TC_materialized_generic_index_record_LIST_push(&nct->materialized_generics,
//                         //                                                (TC_materialized_generic_index_record){
//                         //                                                    *type,
//                         //                                                    TC_NIRK_Struct,
//                         //                                                    nth
//                         //                                                });
//
//
//                         const TC_typeid ret = (TC_typeid){
//                             .ty = TC_TY_Struct,
//                             .info.type_id = nth,
//                             .full_typeuse_span = type->full_typeuse_span
//                         };
//
//                         {
//                             size_t_LIST in_progress_structs = size_t_LIST_new();
//                             size_t_LIST in_progress_enums = size_t_LIST_new();
//
//                             // path_size_align_for_materialized_type(&ret, nct, &in_progress_structs, &in_progress_enums,
//                                                                   // target_info, source_info);
//
//                             size_t_LIST_free(&in_progress_structs);
//                             size_t_LIST_free(&in_progress_enums);
//                         }
//
//
//                         return ret;
//                     }
//                 }
//             }
//
//             UNREACHABLE()
//         }
//     }
//     UNREACHABLE()
// }



// SizeAlign path_size_align_for_materialized_type(
//     const TC_typeid* restrict type,
//     const TC_TypeTable* restrict nct,
//
//     // size_t_LIST* restrict in_progress_structs,
//     // size_t_LIST* restrict in_progress_enums,
//     TC_layout_dependency_LIST* restrict in_progress,
//
//
//     const TargetInfo* restrict target_info,
//     const SourceInfo* restrict source_info
// ) {
//     switch (type->ty) {
//         case TC_TY_Void: {
//             INTERNAL_ERROR("void type does not have size or align")
//         }
//         case TC_TY_External_Type: {
//             const SV msg = SV_from_str(
//                 "external type has unknow size and therefore cannot be used alone, use it with for example Ref<T>");
//             syntax_error(&source_info->source, &source_info->source_filename, &type->info.external_type.span.sv, &msg);
//             exit(1);
//         }
//         case TC_TY_Primitive: {
//             return (SizeAlign){TC_Primitive_TY_size(type->info.primitive), TC_Primitive_TY_size(type->info.primitive)};
//         }
//         case TC_TY_Slice: {
//             return (SizeAlign){target_info->ptr_size * 2, target_info->ptr_size}; // { ptr; len; }
//         }
//         case TC_TY_Array: {
//             const SizeAlign inner = path_size_align_for_materialized_type(
//                 type->info.t_array.contained, nct, in_progress_structs, in_progress_enums, target_info, source_info);
//             return (SizeAlign){
//                 type->info.t_array.array_size * inner.size,
//                 inner.align
//             };
//         }
//         case TC_TY_List: {
//             return (SizeAlign){target_info->ptr_size * 3, target_info->ptr_size}; // { ptr; len; cap; }
//         }
//         case TC_TY_Ref:
//         case TC_TY_Box: {
//             return (SizeAlign){target_info->ptr_size, target_info->ptr_size};
//         }
//         case TC_TY_Struct: {
//             TC_structdef* item = &nct->structs.array[type->info.type_id];
//
//             // already calculated
//             if (item->layout.size != 0) {
//                 return (SizeAlign){item->layout.size, item->layout.align};
//             }
//
//             for (size_t n = 0; n < in_progress_structs->len; n++) {
//                 if (in_progress_structs->array[n] == type->info.type_id) {
//                     // cycle detected
//                     const SV msg = SV_from_str(
//                         "owned cycles are forbidden, use Box<T> if you need to construct cycle in runtime");
//                     syntax_error(&source_info->source, &source_info->source_filename, &item->name.span.sv, &msg);
//                     exit(1);
//                 }
//             }
//
//             // calc
//             size_t_LIST_push(in_progress_structs, type->info.type_id); // mark as in_progress
//             const size_t this_in_progress_nth = in_progress_structs->len - 1;
//
//             uint32_t* field_offsets = malloc(sizeof(uint32_t) * item->fields.len);
//
//
//             uint32_t offset = 0;
//             uint32_t max_align = 1;
//
//             for (uint32_t i = 0; i < item->fields.len; i++) {
//                 const TC_typeid* ft = &item->fields.array[i].type;
//
//                 const SizeAlign sa = path_size_align_for_materialized_type(
//                     ft, nct, in_progress_structs, in_progress_enums, target_info, source_info);
//
//                 const uint32_t fa = sa.align;
//                 const uint32_t fs = sa.size;
//
//                 offset = (offset + fa - 1) & ~(fa - 1); // align up
//                 field_offsets[i] = offset;
//                 offset += fs;
//                 if (fa > max_align) max_align = fa;
//             }
//
//             const TC_struct_layout sl = (TC_struct_layout){
//                 (offset + max_align - 1) & ~(max_align - 1), // tail padding,
//                 max_align,
//                 field_offsets
//             };
//             item->layout = sl; // write to the struct
//
//             // done
//             in_progress_structs->array[this_in_progress_nth] = SIZE_MAX;
//
//             return (SizeAlign){item->layout.size, item->layout.align};
//         }
//         case TC_TY_Enum: {
//             TC_enumdef* item = &nct->enums.array[type->info.type_id];
//
//             if (item->size != 0) {
//                 return (SizeAlign){item->size, item->align};
//             }
//
//             for (size_t n = 0; n < in_progress_enums->len; n++) {
//                 if (in_progress_enums->array[n] == type->info.type_id) {
//                     const SV msg = SV_from_str(
//                         "owned cycles are forbidden, use Box<T> if you need to construct cycle in runtime");
//                     syntax_error(&source_info->source, &source_info->source_filename, &item->name.span.sv, &msg);
//                     exit(1);
//                 }
//             }
//
//             size_t_LIST_push(in_progress_enums, type->info.type_id);
//             const size_t this_in_progress_nth = in_progress_enums->len - 1;
//
//             // discriminant is a primitive integer, align == size
//             size_t disc_size;
//             if (item->variants.len <= 0xFF) disc_size = 1;
//             else if (item->variants.len <= 0xFFFF) disc_size = 2;
//             else if (item->variants.len <= 0xFFFFFFFF) disc_size = 4;
//             else disc_size = 8;
//
//             size_t max_payload_size = 0;
//             size_t max_payload_align = 1;
//
//             for (size_t v = 0; v < item->variants.len; v++) {
//                 const TC_enumdef_variant* variant = &item->variants.array[v];
//
//                 size_t payload_size = 0;
//                 size_t variant_align = 1;
//
//                 for (size_t f = 0; f < variant->values_len; f++) {
//                     const SizeAlign sa = path_size_align_for_materialized_type(
//                         &variant->values[f],
//                         nct,
//                         in_progress_structs, in_progress_enums,
//                         target_info, source_info
//                     );
//                     payload_size = (payload_size + sa.align - 1) & ~(sa.align - 1);
//                     payload_size += sa.size;
//                     if (sa.align > variant_align) variant_align = sa.align;
//                 }
//
//                 // round variant payload up to its own alignment
//                 payload_size = (payload_size + variant_align - 1) & ~(variant_align - 1);
//
//                 if (payload_size > max_payload_size) max_payload_size = payload_size;
//                 if (variant_align > max_payload_align) max_payload_align = variant_align;
//             }
//
//             const size_t total_align = disc_size > max_payload_align ? disc_size : max_payload_align;
//             const size_t payload_start = (disc_size + max_payload_align - 1) & ~(max_payload_align - 1);
//             const size_t raw_size = payload_start + max_payload_size;
//             const size_t total_size = (raw_size + total_align - 1) & ~(total_align - 1);
//
//             item->size = total_size;
//             item->align = total_align;
//             item->discriminant_size = disc_size;
//
//             in_progress_enums->array[this_in_progress_nth] = SIZE_MAX;
//
//             return (SizeAlign){total_size, total_align};
//         }
//     }
//     UNREACHABLE("type->ty = '%u'", type->ty);
// }