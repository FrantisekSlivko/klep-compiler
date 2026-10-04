#ifndef KLEPCOMPILER_LIST_H
#define KLEPCOMPILER_LIST_H
#include <stdbool.h>
#include <stdlib.h>

#define init_const_list_type(NAME,PREFIX) typedef struct PREFIX { \
    NAME* array; \
    size_t len; \
} PREFIX;


#define init_list_type(NAME,PREFIX) typedef struct PREFIX { \
    NAME* array; \
    size_t cap; \
    size_t len; \
} PREFIX; \
\
\
__attribute__((unused)) static inline PREFIX PREFIX ## _new (void) { \
    NAME* array = malloc(sizeof(NAME)*4); \
    return (PREFIX) { .array=array, .cap=4, .len=0  }; \
} \
__attribute__((unused)) static inline PREFIX PREFIX ## _new_with_capacity (size_t capacity) { \
    NAME* array = malloc(sizeof(NAME)*capacity); \
    return (PREFIX) { .array=array, .cap=capacity, .len=0  }; \
} \
\
__attribute__((unused)) static inline void PREFIX ## _free(PREFIX* instance) { \
    free(instance->array); \
} \
\
__attribute__((unused)) static inline void PREFIX ## _push(PREFIX* instance, NAME item) { \
    if (instance->len + 1 > instance->cap) { \
        instance->cap *= 2; \
        if (instance->cap == 0) instance->cap = 4; \
        NAME* new_array = realloc(instance->array, sizeof(NAME)*instance->cap); \
        if (!new_array) return; \
        instance->array = new_array; \
    } \
    instance->array[(instance->len)++] = item; \
} \
\
__attribute__((unused)) static inline NAME* PREFIX ## _pop(PREFIX* instance) { \
    if (instance->len < 1) { \
        return NULL; \
    } \
    return &instance->array[--(instance->len)]; \
} \
\
__attribute__((unused)) static inline NAME* PREFIX ## _peek_last(PREFIX* instance) { \
    if (instance->len < 1) { \
        return NULL; \
    } \
    return &instance->array[instance->len - 1]; \
} \
__attribute__((unused)) static inline bool PREFIX ## _is_empty(PREFIX* instance) { \
    return instance->len < 1; \
} \
__attribute__((unused)) static inline void PREFIX ## _shrink(PREFIX* instance) { \
    NAME* new_array = realloc(instance->array, sizeof(NAME)*instance->len); \
    if (!new_array) return; \
    instance->array = new_array; \
    instance->cap = instance->len; \
} \
\
__attribute__((unused)) static inline void PREFIX ## _inflate(PREFIX* instance, size_t to_size) { \
    NAME* new_array = realloc(instance->array, sizeof(NAME)*to_size); \
    instance->len = to_size;\
    if (!new_array) return; \
    instance->array = new_array; \
    instance->cap = instance->len; \
} \
\
typedef struct { \
    const PREFIX* ref; \
    size_t pos; \
} PREFIX ## View; \
\
__attribute__((unused)) static inline PREFIX ## View PREFIX ## _to_view(PREFIX* instance) { \
    return (PREFIX ## View) { .ref=instance, .pos=0 }; \
} \
\
\
__attribute__((unused)) static inline bool PREFIX ## View_has_next(PREFIX ## View* instance) { \
    return instance->pos < instance->ref->len; \
} \
__attribute__((unused)) static inline const NAME* PREFIX ## View_next(PREFIX ## View* instance) { \
    if (instance->pos < instance->ref->len) \
        return &instance->ref->array[(instance->pos)++]; \
    return NULL; \
} \
__attribute__((unused)) static inline const NAME* PREFIX ## View_peek(PREFIX ## View* instance) { \
    if (instance->pos < instance->ref->len)\
        return &instance->ref->array[instance->pos]; \
    return NULL; \
} \





#define init_prefixed_list_type(x) init_list_type(x, x ## _LIST)


#endif //KLEPCOMPILER_LIST_H
