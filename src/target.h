#ifndef KLEPCOMPILER_TARGET_H
#define KLEPCOMPILER_TARGET_H
#include <stdint.h>


typedef struct {
    /// in bytes
    uint8_t ptr_size;
    /// in bytes (on most architectures equal ptr_size)
    uint8_t f64_align;
    /// bytes size of biggest common reg
    uint8_t reg_size;
} TargetInfo;


#endif //KLEPCOMPILER_TARGET_H
