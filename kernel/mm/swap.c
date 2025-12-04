#include <os/mm.h>

int swap_location;

// NOTE: do not swapout pagedir in grouop, only swapout leaf nodes
kva_t swapout(pageframe_group_t* group) {
    return 0;
}
