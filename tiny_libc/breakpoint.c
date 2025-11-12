#define BREAKPOINT_CNT_LOC 0x5ffffffc  // watch in .gdbinit

void breakpoint() { *((int*)BREAKPOINT_CNT_LOC) += 1; }
