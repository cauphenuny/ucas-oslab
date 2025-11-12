#include "breakpoint.h"

#define BREAKPOINT_CNT_LOC 0x5ffffffc  // watch in .gdbinit

static inline __attribute__((__always_inline__)) void breakpoint_trigger(breakpoint_level_t level) {
    if (level <= BRK_LEVEL) {
        *((int*)BREAKPOINT_CNT_LOC) += 1;
    }
}

void breakpoint(void) { breakpoint_trigger(BRK_ALWAYS); }

void breakpoint_set(breakpoint_level_t level) { breakpoint_trigger(level); }
