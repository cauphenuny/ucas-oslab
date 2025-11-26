#include <os/kernel.h>
#include <os/string.h>
#include <os/task.h>
#include <type.h>

#define min(a, b) ((a) < (b) ? (a) : (b))

uint64_t load_task_img(task_info_t task) {
    /**
     * DONE:
     * 1. [p1-task3] load task from image via task id, and return its entrypoint
     * 2. [p1-task4] load task via task name, thus the arg should be 'char *taskname'
     */

    uint8_t buffer[SECTOR_SIZE];

    uint64_t dest = task.entrance;
    int offset = task.phyaddr_start % SECTOR_SIZE;
    int src_blockid = task.phyaddr_start / SECTOR_SIZE;
    int aligned_phyaddr = task.phyaddr_start - offset;
    int nblocks = NBYTES2SEC(task.phyaddr_end - aligned_phyaddr);
    int sum_len = task.phyaddr_end - task.phyaddr_start;

    for (int i = 0; i < nblocks; i++) {
        bios_sd_read((uint64_t)buffer, 1, src_blockid + i);
        int delta_len = min(sum_len, SECTOR_SIZE - offset);
        memcpy((void*)dest, buffer + offset, delta_len);
        dest += delta_len;
        sum_len -= delta_len;
        offset = 0;
    }
    // bios_sd_read(LOAD_TEMP_ADDR, nblocks, src_blockid);
    // memcpy((void*)dest, (void*)LOAD_TEMP_ADDR + offset, task.phyaddr_end - task.phyaddr_start);
    // bios_putstr("Loaded.\n");
    return task.entrance;
}
