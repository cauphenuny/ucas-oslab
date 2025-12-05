#ifndef __INCLUDE_TASK_H__
#define __INCLUDE_TASK_H__

#include <os/sched.h>
#include <type.h>

#define TASK_MAXNUM 32
#define TASK_SIZE   0x10000

#define SECTOR_SIZE        512
#define NBYTES2SEC(nbytes) (((nbytes) / SECTOR_SIZE) + ((nbytes) % SECTOR_SIZE != 0))

/* TODO: [p1-task4] implement your own task_info_t! */
typedef struct task_info {
    char name[16];
    int phyaddr;  // on SD-card
    int filesize;
    int memsize;  // p_memsz, physical memory size
    uint64_t entrance;
} task_info_t;

extern int task_num;
extern task_info_t tasks[TASK_MAXNUM];

task_info_t* find_task(const char* name);
void show_tasks();
void fetch_pcb_info(const pcb_t* pcb, ptr_t* kernel_ra, ptr_t* user_ra);
pcb_t* construct_pcb(
    const task_info_t* task, uint64_t entrance, int argc, char* argv[], int kernel_mem,
    int user_mem);
int set_proc_affinity(pcb_t* pcb, unsigned affinity_mask);
void init_pcb(void);

#endif
