#ifndef KLEPCOMPILER_DEV_H
#define KLEPCOMPILER_DEV_H

#define TO_STRING_HELPER(x) #x
#define TO_STRING(x) TO_STRING_HELPER(x)

#define LINE_STR TO_STRING(__LINE__)

// #define __LINE_STR__ STR(__LINE__)

#define UNIMPLEMENTED(fmt, ...) do {                                                 \
    fprintf(stderr, __FILE__ ":" LINE_STR ": UNIMPLEMENTED: " fmt "\n" __VA_OPT__(,) __VA_ARGS__);  \
    exit(2);                                                                         \
} while (0);


#define INTERNAL_ERROR(fmt, ...) do {                                                 \
    fprintf(stderr, "[INTERNAL at "__FILE__ ":" LINE_STR ": ] " fmt "\n" __VA_OPT__(,) __VA_ARGS__); \
    exit(3);                                                                          \
} while (0);


#endif //KLEPCOMPILER_DEV_H
