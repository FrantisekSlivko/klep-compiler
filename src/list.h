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
static inline PREFIX PREFIX ## _new (void) { \
    NAME* array = malloc(sizeof(NAME)*4); \
    return (PREFIX) { .array=array, .cap=4, .len=0  }; \
} \
\
static inline void PREFIX ## _free(PREFIX* instance) { \
    free(instance->array); \
} \
\
static inline void PREFIX ## _push(PREFIX* instance, NAME item) { \
    if (instance->len + 1 > instance->cap) { \
        instance->cap *= 2;\
        NAME* new_array = realloc(instance->array, sizeof(NAME)*instance->cap); \
        if (!new_array) return; \
        instance->array = new_array; \
    } \
    instance->array[(instance->len)++] = item; \
} \
\
static inline NAME* PREFIX ## _pop(PREFIX* instance) { \
    if (instance->len < 1) { \
        return NULL; \
    } \
    return &instance->array[--(instance->len)]; \
} \
\
static inline NAME* PREFIX ## _peek_last(PREFIX* instance) { \
if (instance->len < 1) { \
return NULL; \
} \
return &instance->array[instance->len - 1]; \
} \
static inline bool PREFIX ## _is_empty(PREFIX* instance) { \
    return instance->len < 1; \
} \
\
typedef struct { \
    const PREFIX* ref; \
    size_t pos; \
} PREFIX ## View; \
\
static inline PREFIX ## View PREFIX ## _to_view(PREFIX* instance) { \
    return (PREFIX ## View) { .ref=instance, .pos=0 }; \
} \
\
\
static inline bool PREFIX ## View_has_next(PREFIX ## View* instance) { \
    return instance->pos < instance->ref->len; \
} \
static inline const NAME* PREFIX ## View_next(PREFIX ## View* instance) { \
    if (instance->pos < instance->ref->len) \
        return &instance->ref->array[(instance->pos)++]; \
    return NULL; \
} \
static inline const NAME* PREFIX ## View_peek(PREFIX ## View* instance) { \
    if (instance->pos < instance->ref->len)\
        return &instance->ref->array[instance->pos]; \
    return NULL; \
} \





#define init_prefixed_list_type(x) init_list_type(x, x ## _LIST)


#endif //KLEPCOMPILER_LIST_H
