#ifndef KLEPCOMPILER_DEV_H
#define KLEPCOMPILER_DEV_H



#define UNIMPLEMENTED(m) do {             \
    fprintf(stderr, "unimplemented: ");   \
    fprintf(stderr, m);                   \
    fprintf(stderr, "\n");                \
    exit(2);                              \
} while (0);

#endif //KLEPCOMPILER_DEV_H
