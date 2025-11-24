#ifndef __INCLUDE_TASK_H__
#define __INCLUDE_TASK_H__

#include <os/sched.h>
#include <type.h>

#define TASK_MEM_BASE 0x52000000
#define TASK_MAXNUM   16
#define TASK_SIZE     0x10000

#define TASK_NUM_LOC (OS_SIZE_LOC + 2)

#define SECTOR_SIZE        512
#define NBYTES2SEC(nbytes) (((nbytes) / SECTOR_SIZE) + ((nbytes) % SECTOR_SIZE != 0))

/* TODO: [p1-task4] implement your own task_info_t! */
typedef struct {
    char name[16];
    int phyaddr_start, phyaddr_end;
    uint64_t entrance;
} task_info_t;

extern int task_num;
extern task_info_t tasks[TASK_MAXNUM];

task_info_t* find_task(const char* name);
void show_tasks(void);
void fetch_pcb_info(const pcb_t* pcb, ptr_t* kernel_ra, ptr_t* user_ra);
void init_pcb_stack(
    pcb_t* pcb, ptr_t kernel_stack, ptr_t user_stack, ptr_t entry_point, int argc, char** argv);
pcb_t* construct_pcb(const char* name, int argc, char* argv[], int kernel_mem, int user_mem);
void set_proc_affinity(pcb_t* pcb, unsigned affinity_mask);

#endif
