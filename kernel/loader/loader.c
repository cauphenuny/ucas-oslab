#include <os/kernel.h>
#include <os/string.h>
#include <os/task.h>
#include <type.h>

uint64_t load_task_img(task_info_t task) {
    /**
     * TODO:
     * 1. [p1-task3] load task from image via task id, and return its entrypoint
     * 2. [p1-task4] load task via task name, thus the arg should be 'char *taskname'
     */

    int entrance = task.entrance;
    int offset = task.phyaddr_start % SECTOR_SIZE;
    int dest = entrance - offset;
    int src_blockid = task.phyaddr_start / SECTOR_SIZE;
    int aligned_phyaddr = task.phyaddr_start - offset;
    int nblocks = NBYTES2SEC(task.phyaddr_end - aligned_phyaddr);
    bios_sd_read(dest, nblocks, src_blockid);
    // bios_putstr("Loaded.\n");

    return entrance;
}
