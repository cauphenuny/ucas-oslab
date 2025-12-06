#include <logger.h>
#include <os/kernel.h>
#include <os/loader.h>
#include <os/mm.h>
#include <os/string.h>
#include <os/task.h>
#include <type.h>

kva_t create_task_pgdir(const task_info_t* task) {
    kva_t pgdir = new_top_pgdir(get_current_pagegroup());
    pretty_logi("allocated pgdir at 0x%lx for task %s", pgdir, task->name);
    for (uva_t va = task->entrance; va < task->entrance + task->memsize; va += PAGE_SIZE) {
        alloc_page(va, pgdir, false);
    }
    return pgdir;
}

uint64_t load_task_img(const task_info_t* task, kva_t pgdir) {
    /**
     * DONE:
     * 1. [p1-task3] load task from image via task id, and return its entrypoint
     * 2. [p1-task4] load task via task name, thus the arg should be 'char *taskname'
     */

    uint8_t buffer[SECTOR_SIZE];

    uva_t dest = task->entrance;
    int offset = task->phyaddr % SECTOR_SIZE;
    int src_blockid = task->phyaddr / SECTOR_SIZE;
    int aligned_phyaddr = task->phyaddr - offset;
    int nblocks = NBYTES2SEC((task->phyaddr + task->filesize) - aligned_phyaddr);
    int sum_len = task->filesize;

    for (int i = 0; i < nblocks; i++) {
        bios_sd_read((uint64_t)buffer, 1, src_blockid + i);
        int delta_len = min(sum_len, SECTOR_SIZE - offset);
        memcpy_kva2uva(dest, (kva_t)(buffer + offset), delta_len, pgdir);
        dest += delta_len;
        sum_len -= delta_len;
        offset = 0;
    }

    return task->entrance;
}
