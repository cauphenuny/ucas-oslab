/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *            Copyright (C) 2018 Institute of Computing Technology, CAS
 *               Author : Han Shukai (email : hanshukai@ict.ac.cn)
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *        Process scheduling related content, such as: scheduler, process blocking,
 *                 process wakeup, process creation, process kill, etc.
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * */

#ifndef INCLUDE_SCHEDULER_H_
#define INCLUDE_SCHEDULER_H_

#include <asm.h>
#include <asm/regs.h>
#include <os/list.h>
#include <type.h>
#include <os/smp.h>

#define NUM_MAX_TASK 16

#define REG_ZERO (OFFSET_REG_ZERO >> RISCV_LGPTR)
#define REG_RA   (OFFSET_REG_RA >> RISCV_LGPTR)
#define REG_SP   (OFFSET_REG_SP >> RISCV_LGPTR)
#define REG_GP   (OFFSET_REG_GP >> RISCV_LGPTR)
#define REG_TP   (OFFSET_REG_TP >> RISCV_LGPTR)
#define REG_T0   (OFFSET_REG_T0 >> RISCV_LGPTR)
#define REG_T1   (OFFSET_REG_T1 >> RISCV_LGPTR)
#define REG_T2   (OFFSET_REG_T2 >> RISCV_LGPTR)
#define REG_S0   (OFFSET_REG_S0 >> RISCV_LGPTR)
#define REG_S1   (OFFSET_REG_S1 >> RISCV_LGPTR)
#define REG_A0   (OFFSET_REG_A0 >> RISCV_LGPTR)
#define REG_A1   (OFFSET_REG_A1 >> RISCV_LGPTR)
#define REG_A2   (OFFSET_REG_A2 >> RISCV_LGPTR)
#define REG_A3   (OFFSET_REG_A3 >> RISCV_LGPTR)
#define REG_A4   (OFFSET_REG_A4 >> RISCV_LGPTR)
#define REG_A5   (OFFSET_REG_A5 >> RISCV_LGPTR)
#define REG_A6   (OFFSET_REG_A6 >> RISCV_LGPTR)
#define REG_A7   (OFFSET_REG_A7 >> RISCV_LGPTR)
#define REG_S2   (OFFSET_REG_S2 >> RISCV_LGPTR)
#define REG_S3   (OFFSET_REG_S3 >> RISCV_LGPTR)
#define REG_S4   (OFFSET_REG_S4 >> RISCV_LGPTR)
#define REG_S5   (OFFSET_REG_S5 >> RISCV_LGPTR)
#define REG_S6   (OFFSET_REG_S6 >> RISCV_LGPTR)
#define REG_S7   (OFFSET_REG_S7 >> RISCV_LGPTR)
#define REG_S8   (OFFSET_REG_S8 >> RISCV_LGPTR)
#define REG_S9   (OFFSET_REG_S9 >> RISCV_LGPTR)
#define REG_S10  (OFFSET_REG_S10 >> RISCV_LGPTR)
#define REG_S11  (OFFSET_REG_S11 >> RISCV_LGPTR)
#define REG_T3   (OFFSET_REG_T3 >> RISCV_LGPTR)
#define REG_T4   (OFFSET_REG_T4 >> RISCV_LGPTR)
#define REG_T5   (OFFSET_REG_T5 >> RISCV_LGPTR)
#define REG_T6   (OFFSET_REG_T6 >> RISCV_LGPTR)

#define REG_SSTATUS  (OFFSET_REG_SSTATUS >> RISCV_LGPTR)
#define REG_SEPC     (OFFSET_REG_SEPC >> RISCV_LGPTR)
#define REG_SBADADDR (OFFSET_REG_SBADADDR >> RISCV_LGPTR)
#define REG_SCAUSE   (OFFSET_REG_SCAUSE >> RISCV_LGPTR)

/* used to save register infomation */
typedef struct regs_context {
    /* Saved main processor registers.*/
    reg_t regs[32];

    /* Saved special registers. */
    reg_t sstatus;
    reg_t sepc;
    reg_t sbadaddr;
    reg_t scause;
} regs_context_t;

enum {
    SWITCHTO_REG_RA,
    SWITCHTO_REG_SP,
    SWITCHTO_REG_S0,
    SWITCHTO_REG_S1,
    SWITCHTO_REG_S2,
    SWITCHTO_REG_S3,
    SWITCHTO_REG_S4,
    SWITCHTO_REG_S5,
    SWITCHTO_REG_S6,
    SWITCHTO_REG_S7,
    SWITCHTO_REG_S8,
    SWITCHTO_REG_S9,
    SWITCHTO_REG_S10,
    SWITCHTO_REG_S11,
};

/* used to save register infomation in switch_to */
typedef struct switchto_context {
    /* Callee saved registers.*/
    reg_t regs[14];
} switchto_context_t;

typedef enum {
    TASK_BLOCKED,
    TASK_RUNNING,
    TASK_READY,
    TASK_EXITED,
} task_status_t;

/* Process Control Block */
typedef struct pcb {
    /* register context */
    // NOTE: this order must be preserved, which is defined in regs.h!!
    reg_t kernel_sp;
    reg_t user_sp;
    ptr_t kernel_stack_base;
    ptr_t user_stack_base;

    /* previous, next pointer */
    list_node_t list;  // NOTE: used for scheduling queues, only able to be in one queue
    list_head wait_list;

    /* process id */
    pid_t pid;

    /* BLOCK | READY | RUNNING */
    task_status_t status;

    /* cursor position */
    int cursor_x;
    int cursor_y;

    /* time(seconds) to wake up sleeping PCB */
    uint64_t wakeup_time;

    /* process name */
    char name[16];

    /* process workload */
    int task_id;
    int task_workload;
    int slice_cnt;
} pcb_t;

/* ready queue to run */
extern list_head ready_queue;

/* sleep queue to be blocked in */
extern list_head sleep_queue;

/* current running task PCB */
register pcb_t* current_running asm("tp");
extern pid_t process_id;

extern pcb_t pcb[NUM_MAX_TASK];
extern pcb_t* kernel_pcb[NR_CPUS];

pcb_t* alloc_pcb();

extern void switch_to(pcb_t* prev, pcb_t* next);
void do_scheduler(void);
void do_sleep(uint32_t);

void do_block(list_node_t*, list_head* queue);
void do_unblock(list_node_t*);

void unblock_list(list_head* list, const char* name);

void set_process_workload(int workload);

void print_sched_queue(const list_head* queue, const char* name);

/************************************************************/
/* TODO [P3-TASK1] exec exit kill waitpid ps*/
#ifdef S_CORE
extern pid_t do_exec(int id, int argc, uint64_t arg0, uint64_t arg1, uint64_t arg2);
#else
extern pid_t do_exec(char* name, int argc, char* argv[]);
#endif
extern void do_exit(void);
extern int do_kill(pid_t pid);
extern int do_waitpid(pid_t pid);
extern void do_process_show();
extern pid_t do_getpid();
/************************************************************/

#endif
