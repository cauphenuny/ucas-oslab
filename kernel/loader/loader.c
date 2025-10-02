#include <os/kernel.h>
#include <os/string.h>
#include <os/task.h>
#include <type.h>

uint64_t load_task_img(int taskid) {
    /**
     * TODO:
     * 1. [p1-task3] load task from image via task id, and return its entrypoint
     * 2. [p1-task4] load task via task name, thus the arg should be 'char *taskname'
     */

    int dest = TASK_MEM_BASE + taskid * TASK_SIZE;
    int src_bytes = TASK_SIZE * (taskid + 1);
    int src_block = src_bytes / SECTOR_SIZE;
    int nblocks = TASK_SIZE / SECTOR_SIZE;

    bios_sd_read(dest, nblocks, src_block);

    return dest;
}
