#include "str.h"

#include <string.h>


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
