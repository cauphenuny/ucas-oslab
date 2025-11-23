#include <asm/regs.h>
#include <assert.h>
#include <breakpoint.h>
#include <logger.h>
#include <os/list.h>
#include <os/lock.h>
#include <os/mm.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/task.h>
#include <os/time.h>
#include <printk.h>
#include <screen.h>

pcb_t pcb[NUM_MAX_TASK];
const ptr_t pid0_stack = INIT_KERNEL_STACK + PAGE_SIZE;
pcb_t pid0_pcb = {
    .kernel_sp = (ptr_t)pid0_stack,
    .user_sp = (ptr_t)pid0_stack,
    .wait_list = {&pid0_pcb.wait_list, &pid0_pcb.wait_list},
    .pid = 0,
    .name = "init",
};

pcb_t* alloc_pcb() {
    pcb_t* selected_pcb = NULL;
    for (int i = 0; i < NUM_MAX_TASK; i++) {
        if (pcb[i].status == TASK_EXITED) {
            selected_pcb = &pcb[i];
            break;
        }
    }
    if (!selected_pcb) {
        pretty_log(LOG_ERROR, "no free PCB!");
        return NULL;
    }
    return selected_pcb;
}

pcb_t* find_pcb(pid_t pid) {
    for (int i = 0; i < NUM_MAX_TASK; i++) {
        if (pcb[i].pid == pid && pcb[i].status != TASK_EXITED) {
            return &pcb[i];
        }
    }
    return NULL;
}

void free_pcb(pcb_t* pcb) {
    if (!pcb) return;
    pcb->status = TASK_EXITED;
}

LIST_HEAD(ready_queue);
LIST_HEAD(sleep_queue);

/* global process id */
pid_t process_id = 1;

// #define SCHED_FRAME_OFFSET   "72"
// #define SCHED_FRAME_OFFSET_I 72
//
// #define LOAD_SCHED_RA(var, sp) \
//     asm volatile("ld %0, " SCHED_FRAME_OFFSET "(%1)" : "=r"(var) : "r"(sp))

void print_sched_queue(const list_head* queue, const char* name) {
    size_t size = list_size(queue);
    pretty_log(LOG_INFO, "there are %d tasks in the %s.", size, name);
    list_node_t* current = queue->next;
    while (current != queue) {
        pcb_t* pcb = container_of(current, pcb_t, list);
        // ptr_t ra1;
        // LOAD_SCHED_RA(ra1, pcb->user_sp);
        pretty_log(
            LOG_DEBUG, "(%d) %s: status=%d, node=0x%x", pcb->pid, pcb->name, pcb->status, current);
        current = current->next;
    }
}

#define TIME_SLICE_HISTORY_SIZE 100

pcb_t* time_slice_history[TIME_SLICE_HISTORY_SIZE];
int time_slice_history_index;

pcb_t* pick_process() {
    assert(ready_queue.next != &ready_queue);
    int min_task_id = 0x7f7f7f7f;
    int min_slice_cnt = 0x7f7f7f7f;
    // return container_of(ready_queue.next, pcb_t, list);
    pcb_t* selected_proc = NULL;
    assert(ready_queue.next != &ready_queue);
    list_foreach_node(iter, &ready_queue) {
        pcb_t* proc = container_of(iter, pcb_t, list);
        int normalized_cnt = proc->slice_cnt / (proc->task_workload + 1);
        if (proc->pid != 0 && proc->task_id < min_task_id) {
            selected_proc = proc;
            min_task_id = proc->task_id;
            min_slice_cnt = normalized_cnt;
        } else if (proc->task_id == min_task_id) {
            if (normalized_cnt < min_slice_cnt) {
                selected_proc = proc;
                min_slice_cnt = normalized_cnt;
            }
        }
    }
    if (!selected_proc) {
        pretty_loge("no candidate selected, fallback to first (pid 0)");
        selected_proc = container_of(ready_queue.next, pcb_t, list);
    }
    assert(selected_proc);

    pretty_log(
        LOG_DEBUG, "selected pid %d (task_id=%d, workload=%d, slice_cnt=%d)", selected_proc->pid,
        selected_proc->task_id, selected_proc->task_workload, selected_proc->slice_cnt);
    if (time_slice_history[time_slice_history_index]) {
        time_slice_history[time_slice_history_index]->slice_cnt--;
    }
    time_slice_history[time_slice_history_index] = selected_proc;
    selected_proc->slice_cnt++;
    time_slice_history_index = (time_slice_history_index + 1) % TIME_SLICE_HISTORY_SIZE;
    return selected_proc;
}

void do_scheduler(void) {
    // asm volatile("mv %0, sp" : "=r"(sp));
    // printk("pid: %d, sp: 0x%x", current_running->pid, sp);
    // TODO: [p2-task3] Check sleep queue to wake up PCBs

    check_sleeping();

    /************************************************************/
    /* Do not touch this comment. Reserved for future projects. */
    /************************************************************/

    // TODO: [p2-task1] Modify the current_running pointer.

    if (current_running->status == TASK_RUNNING) {
        current_running->status = TASK_READY;
        list_append(&ready_queue, &current_running->list);
    }
    print_sched_queue(&ready_queue, "ready_queue");
    pcb_t* next_running = pick_process();
    list_delete(&next_running->list);
    pretty_log(
        LOG_INFO, "switch from pid %d(%s) to pid %d(%s).", current_running->pid,
        current_running->name, next_running->pid, next_running->name);
    next_running->status = TASK_RUNNING;

    // TODO: [p2-task1] switch_to current_running
    switch_to(current_running, next_running);
    screen_move_cursor(current_running->cursor_x, current_running->cursor_y);

    // breakpoint();
}

void do_sleep(uint32_t sleep_time) {
    // TODO: [p2-task3] sleep(seconds)
    // NOTE: you can assume: 1 second = 1 `timebase` ticks
    // 1. block the current_running
    // 2. set the wake up time for the blocked task
    // 3. reschedule because the current_running is blocked.
    uint64_t current = get_timer(), target = current + sleep_time;
    current_running->wakeup_time = target;
    pretty_log(
        LOG_INFO, "pid %d sleeping for %d seconds (wake at %d)", current_running->pid, sleep_time,
        target);
    // breakpoint();
    do_block(&current_running->list, &sleep_queue);
    do_scheduler();
}

// NOTE: do_block would not delete node from any list
void do_block(list_node_t* pcb_node, list_head* queue) {
    // TODO: [p2-task2] block the pcb task into the block queue
    asserts(!pcb_node->next && !pcb_node->prev, "pcb_node is already in a list");
    pcb_t* pcb = container_of(pcb_node, pcb_t, list);
    pretty_log(LOG_INFO, "blocking pid %d(status=%d)", pcb->pid, pcb->status);
    if (pcb->status == TASK_BLOCKED) return;
    pcb->status = TASK_BLOCKED;
    list_append(queue, pcb_node);
}

/**
 * @brief unblock the `pcb` to ready queue
 */
void do_unblock(list_node_t* pcb_node) {
    // TODO: [p2-task2] unblock the `pcb` from the block queue
    pcb_t* pcb = container_of(pcb_node, pcb_t, list);
    if (pcb->status != TASK_BLOCKED) {
        pretty_log(
            LOG_WARN, "unblocking a non-blocked task(pid=%d, status=%d)", pcb->pid, pcb->status);
    }
    pcb->status = TASK_READY;
    list_delete(pcb_node);
    list_append(&ready_queue, pcb_node);
}

void exit_wakeup(pcb_t* pcb) {
    for (list_node_t *node = pcb->wait_list.next, *next; node != &pcb->wait_list; node = next) {
        next = node->next;
        pcb_t* wait_pcb = container_of(node, pcb_t, list);
        pretty_log(LOG_INFO, "waking up waiting pid %d on exit of pid %d", wait_pcb->pid, pcb->pid);
        do_unblock(node);
    }
}

void cleanup(pcb_t* pcb) {
    pid_t pid = pcb->pid;
    pretty_log(LOG_INFO, "cleaning up pid %d", pid);
    cleanup_mutex(pid);
    if (list_holding(&pcb->list)) {
        list_delete(&pcb->list);
    }
    exit_wakeup(pcb);
    free_pcb(pcb);
}

pid_t do_exec(char* name, int argc, char* argv[]) {
    pretty_log(LOG_DEBUG, "handling exec for %s", name);
    pcb_t* pcb = construct_pcb(name, argc, argv);
    if (!pcb) {
        pretty_log(LOG_WARN, "exec %s failed!", name);
        return 0;
    }
    pretty_log(LOG_INFO, "exec %s succeeded! pid=%d", name, pcb->pid);
    list_append(&ready_queue, &pcb->list);
    print_sched_queue(&ready_queue, "ready_queue");
    return pcb->pid;
}

void do_process_show() {
    const int PID_LEN = 5;
    const int NAME_LEN = 16;
    const char* status_str[] = {
        [TASK_BLOCKED] = "BLOCKED",
        [TASK_READY] = "READY",
        [TASK_RUNNING] = "RUNNING",
        [TASK_EXITED] = "EXITED",
    };
    printk("PID"), screen_move_cursor_col(PID_LEN);
    printk("NAME"), screen_move_cursor_col(PID_LEN + NAME_LEN);
    printk("STATUS\n");
#define display_proc(proc)                      \
    printk("%d", (proc)->pid);                  \
    screen_move_cursor_col(PID_LEN);            \
    printk("%s", (proc)->name);                 \
    screen_move_cursor_col(PID_LEN + NAME_LEN); \
    printk("%s\n", status_str[(proc)->status]);
    display_proc(&pid0_pcb);
    for (int i = 0; i < NUM_MAX_TASK; i++) {
        pcb_t* proc = &pcb[i];
        if (proc->status == TASK_EXITED) continue;
        display_proc(proc);
    }
#undef display_proc
    return;
}

void do_exit() {
    cleanup(current_running);
    do_scheduler();
}

int do_kill(pid_t pid) {
    pcb_t* pcb = find_pcb(pid);
    if (!pcb || current_running->pid == pid) {
        return 0;
    }
    cleanup(pcb);
    return 1;
}

int do_waitpid(pid_t pid) {
    pcb_t* pcb = find_pcb(pid);
    if (!pcb) {
        return 0;
    }
    list_append(&pcb->wait_list, &current_running->list);
    // list_delete(&current_running->list);
    current_running->status = TASK_BLOCKED;
    do_scheduler();
    return pid;
}

void set_process_workload(int workload) {
    if (workload > current_running->task_workload) {
        current_running->task_id++;
    }
    current_running->task_workload = workload;
}
