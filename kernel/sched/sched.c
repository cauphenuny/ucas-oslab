#include "asm/regs.h"

#include <assert.h>
#include <breakpoint.h>
#include <logger.h>
#include <os/list.h>
#include <os/lock.h>
#include <os/mm.h>
#include <os/sched.h>
#include <os/time.h>
#include <printk.h>
#include <screen.h>

pcb_t pcb[NUM_MAX_TASK];
const ptr_t pid0_stack = INIT_KERNEL_STACK + PAGE_SIZE;
pcb_t pid0_pcb = {
    .pid = 0,
    .kernel_sp = (ptr_t)pid0_stack,
    .user_sp = (ptr_t)pid0_stack,
    .name = "init",
};

LIST_HEAD(ready_queue);
LIST_HEAD(sleep_queue);

/* global process id */
pid_t process_id = 1;

#define SCHED_FRAME_OFFSET   "56"
#define SCHED_FRAME_OFFSET_I 56

#define LOAD_SCHED_RA(var, sp) \
    asm volatile("ld %0, " SCHED_FRAME_OFFSET "(%1)" : "=r"(var) : "r"(sp))

void print_sched_queue(void) {
    size_t size = list_size(&ready_queue);
    screen_move_cursor(0, 15);
    pretty_log(LOG_DEBUG, "there are %d tasks in the ready_queue.", size);
    list_node_t* current = ready_queue.next;
    while (current != &ready_queue) {
        pcb_t* pcb = container_of(current, pcb_t, list);
        ptr_t ra1;
        LOAD_SCHED_RA(ra1, pcb->user_sp);
        pretty_log(
            LOG_DEBUG, "\npid: %d, name: %s, sp: 0x%x/0x%x, ra: 0x%x, ra': 0x%x @0x%x           ",
            pcb->pid, pcb->name, pcb->kernel_sp, pcb->user_sp, *(int*)(pcb->kernel_sp), ra1,
            pcb->user_sp + SCHED_FRAME_OFFSET_I);
        current = current->next;
    }
}

void do_scheduler(void) {
    // asm volatile("mv %0, sp" : "=r"(sp));
    // printk("pid: %d, sp: 0x%x", current_running->pid, sp);
    // TODO: [p2-task3] Check sleep queue to wake up PCBs

    /************************************************************/
    /* Do not touch this comment. Reserved for future projects. */
    /************************************************************/

    // TODO: [p2-task1] Modify the current_running pointer.

    asm volatile("sd sp, %0" ::"m"(current_running->user_sp));
    if (current_running->pid) {
        asm volatile("ld sp, %0" : "=m"(current_running->kernel_sp));
    } else {
        asm volatile("sd sp, %0" ::"m"(current_running->kernel_sp));
    }

    size_t size = list_size(&ready_queue);
    pretty_flog(LOG_DEBUG, "there are %d tasks in the ready_queue.", size);
    print_sched_queue();
    list_node_t* front_node;
    list_append(&ready_queue, &current_running->list);
    list_shift(&ready_queue, &front_node);
    assert(front_node);
    pcb_t* next_running = container_of(front_node, pcb_t, list);
    pretty_log(
        LOG_DEBUG, "switch from pid %d(%s) to pid %d(%s).                ", current_running->pid,
        current_running->name, next_running->pid, next_running->name);
    current_running->status = TASK_READY;
    next_running->status = TASK_RUNNING;

    // TODO: [p2-task1] switch_to current_running
    switch_to(current_running, next_running);

    ptr_t sp, target;
    asm volatile("mv %0, sp" : "=r"(sp));
    LOAD_SCHED_RA(target, sp);
    pretty_log(
        LOG_DEBUG, "return to pid %d(%s) at ra=0x%x.                ", current_running->pid,
        current_running->name, target);
    if (target == 0) {
        breakpoint();
    }
}

void do_sleep(uint32_t sleep_time) {
    // TODO: [p2-task3] sleep(seconds)
    // NOTE: you can assume: 1 second = 1 `timebase` ticks
    // 1. block the current_running
    // 2. set the wake up time for the blocked task
    // 3. reschedule because the current_running is blocked.
}

void do_block(list_node_t* pcb_node, list_head* queue) {
    // TODO: [p2-task2] block the pcb task into the block queue
}

void do_unblock(list_node_t* pcb_node) {
    // TODO: [p2-task2] unblock the `pcb` from the block queue
}
