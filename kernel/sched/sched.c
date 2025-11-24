#include <asm/regs.h>
#include <assert.h>
#include <breakpoint.h>
#include <logger.h>
#include <os/list.h>
#include <os/lock.h>
#include <os/mm.h>
#include <os/sched.h>
#include <os/smp.h>
#include <os/string.h>
#include <os/task.h>
#include <os/time.h>
#include <printk.h>
#include <screen.h>

pcb_t pcb_user[NUM_MAX_TASK];
pcb_t pcb_kernel[NR_CPUS];
pcb_t* pcb_all[NUM_MAX_PCB];

pcb_t* alloc_pcb() {
    pcb_t* selected_pcb = NULL;
    for (int i = 0; i < NUM_MAX_TASK; i++) {
        if (pcb_user[i].status == TASK_EXITED) {
            selected_pcb = &pcb_user[i];
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
    if (pid < NR_CPUS) {
        asserts(pcb_kernel[pid].pid == pid, "kernel pcb broken");
        return &pcb_kernel[pid];
    }
    for (int i = 0; i < NUM_MAX_TASK; i++) {
        if (pcb_user[i].pid == pid && pcb_user[i].status != TASK_EXITED) {
            return &pcb_user[i];
        }
    }
    return NULL;
}

void free_pcb(pcb_t* pcb) {
    if (!pcb) return;
    pcb->status = TASK_EXITED;
}

LIST(ready_queue, "ready");
LIST(sleep_queue, "sleep");

/* global process id */
pid_t process_id = NR_CPUS;

// #define SCHED_FRAME_OFFSET   "72"
// #define SCHED_FRAME_OFFSET_I 72
//
// #define LOAD_SCHED_RA(var, sp) \
//     asm volatile("ld %0, " SCHED_FRAME_OFFSET "(%1)" : "=r"(var) : "r"(sp))

void print_pcb_array(const pcb_t pcb[], int n) {
    for (int i = 0; i < n; i++) {
        if (pcb[i].status == TASK_EXITED) continue;
        ptr_t kernel_ra, user_ra;
        fetch_pcb_info(&pcb[i], &kernel_ra, &user_ra);
        pretty_log(
            LOG_DEBUG, "pid=%d, name=%s, aff=0x%x, stat=%d, chan=%s, kctx=%x/%x, uctx=%x/%x", pcb[i].pid,
            pcb[i].name, pcb[i].affinity, pcb[i].status,
            pcb[i].list.container ? pcb[i].list.container->name : "NULL", kernel_ra,
            pcb[i].kernel_sp, user_ra, pcb[i].user_sp);
    }
}

void print_pcb_list(const list_t* list) {
    size_t size = list_size(list);
    pretty_log(LOG_INFO, "there are %d tasks in the %s/%x channel.", size, list->name, list);
    list_node_t* current = list->head.next;
    while (current != &list->head) {
        pcb_t* pcb = container_of(current, pcb_t, list);
        ptr_t kernel_ra, user_ra;
        fetch_pcb_info(pcb, &kernel_ra, &user_ra);
        pretty_log(
            LOG_DEBUG, "pid=%d, name=%s, stat=%d, chan=%s, kctx=%x/%x, uctx=%x/%x", pcb->pid,
            pcb->name, pcb->status, pcb->list.container ? pcb->list.container->name : "NULL",
            kernel_ra, pcb->kernel_sp, user_ra, pcb->user_sp);
        current = current->next;
    }
}

void print_all_pcb() {
    print_pcb_array(pcb_kernel, NR_CPUS);
    print_pcb_array(pcb_user, NUM_MAX_TASK);
}

#define TIME_SLICE_HISTORY_SIZE (100 * NR_CPUS)

pcb_t* time_slice_history[TIME_SLICE_HISTORY_SIZE];
int time_slice_history_index;
int min_task_id = 0x7f7f7f7f;
int min_slice_cnt = 0x7f7f7f7f;

bool update_by_first(pcb_t* selected, pcb_t* next) {
    if (!selected) {
        return true;
    } else {
        return false;
    }
}

void update_by_consumption_init() {
    min_task_id = 0x7f7f7f7f;
    min_slice_cnt = 0x7f7f7f7f;
}

bool update_by_consumption(pcb_t* selected, pcb_t* proc) {
    int normalized_cnt = proc->slice_cnt / (proc->task_workload + 1);
    if (proc->task_id < min_task_id) {
        min_task_id = proc->task_id;
        min_slice_cnt = normalized_cnt;
        return true;
    } else if (proc->task_id == min_task_id) {
        if (normalized_cnt < min_slice_cnt) {
            min_slice_cnt = normalized_cnt;
            return true;
        }
    }
    return false;
}

bool filter_affinity(pcb_t* proc) {
    int hartid = get_current_cpu_id();
    unsigned mask = 1 << hartid;
    return proc->affinity & mask;
}

bool filterout_kernel(pcb_t* proc) {
    if (proc->pid < NR_CPUS) {
        return false;
    }
    return filter_affinity(proc);
}

pcb_t* pick_process_impl(
    list_t* ready_queue, bool (*filter)(pcb_t* proc), void (*init)(),
    bool (*update)(pcb_t* selected, pcb_t* next)) {
    if (init) init();
    pcb_t* selected = NULL;
    list_foreach_node(iter, &ready_queue->head) {
        pcb_t* next = container_of(iter, pcb_t, list);
        if (!filter(next)) continue;
        if (update(selected, next)) {
            selected = next;
        }
    }
    return selected;
}

pcb_t* pick_process() {
    pcb_t* proc = NULL;
    proc = pick_process_impl(&ready_queue, filterout_kernel, update_by_consumption_init, update_by_consumption);
    if (!proc) {
        pretty_log(LOG_WARN, "process insufficient, may fallback to init");
        proc = pick_process_impl(&ready_queue, filter_affinity, NULL, update_by_first);
    }
    asserts(proc, "no process to run");

    pretty_log(
        LOG_DEBUG, "selected pid %d (task_id=%d, workload=%d, slice_cnt=%d)", proc->pid,
        proc->task_id, proc->task_workload, proc->slice_cnt);
    if (time_slice_history[time_slice_history_index]) {
        time_slice_history[time_slice_history_index]->slice_cnt--;
    }
    time_slice_history[time_slice_history_index] = proc;
    proc->slice_cnt++;
    time_slice_history_index = (time_slice_history_index + 1) % TIME_SLICE_HISTORY_SIZE;
    return proc;
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
    print_all_pcb();
    print_pcb_list(&ready_queue);
    pcb_t* next_running = pick_process();
    list_delete(&next_running->list);
    pretty_log(
        LOG_INFO, "switch from pid %d(%s) to pid %d(%s).", current_running->pid,
        current_running->name, next_running->pid, next_running->name);
    next_running->status = TASK_RUNNING;

    // TODO: [p2-task1] switch_to current_running
    switch_to(current_running, next_running);
    screen_move_cursor(current_running->cursor_x, current_running->cursor_y);
    current_running->cpu = get_current_cpu_id();

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
void do_block(list_node_t* pcb_node, list_t* queue) {
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
    // list_delete(pcb_node);
    list_append(&ready_queue, pcb_node);
}

void unblock_list(list_t* queue, const char* name) {
    list_node_t* node = queue->head.next;
    while (node != &queue->head) {
        list_node_t* next = node->next;
        pretty_log(
            LOG_INFO, "unblocking pid %d from %s", container_of(node, pcb_t, list)->pid, name);
        list_delete(node);
        do_unblock(node);
        node = next;
    }
}

void exit_wakeup(pcb_t* pcb) { unblock_list(&pcb->wait_list, "pcb wait_list"); }

void cleanup_proc(pcb_t* pcb) {
    pid_t pid = pcb->pid;
    pretty_log(LOG_INFO, "cleaning up pid %d", pid);
    cleanup_mutex(pid);
    if (list_holding(&pcb->list)) {
        list_delete(&pcb->list);
    }
    exit_wakeup(pcb);
    free_pcb(pcb);
}

pid_t do_exec(char* name, int argc, char* argv[], unsigned affinity_mask) {
    pretty_log(LOG_DEBUG, "handling exec for %s", name);
    pcb_t* pcb = construct_pcb(name, argc, argv, 1, 4);
    if (!pcb) {
        pretty_log(LOG_WARN, "exec %s failed!", name);
        return 0;
    }
    pretty_log(LOG_INFO, "exec %s succeeded! pid=%d", name, pcb->pid);
    set_proc_affinity(pcb, affinity_mask);
    list_append(&ready_queue, &pcb->list);
    print_all_pcb();
    return pcb->pid;
}

int do_process_show() {
    const int PID_LEN = 5;
    const int NAME_LEN = 16;
    const int STAT_LEN = 10;
    const int CHAN_LEN = 9;
    const int TIME_LEN = 6;
    const char* status_str[] = {
        [TASK_BLOCKED] = "BLOCKED",
        [TASK_READY] = "READY",
        [TASK_RUNNING] = "RUNNING",
        [TASK_EXITED] = "EXITED",
    };
    printk("PID"), screen_move_cursor_col(PID_LEN);
    printk("NAME"), screen_move_cursor_col(PID_LEN + NAME_LEN);
    printk("STATUS"), screen_move_cursor_col(PID_LEN + NAME_LEN + STAT_LEN);
    printk("CHANNEL"), screen_move_cursor_col(PID_LEN + NAME_LEN + STAT_LEN + CHAN_LEN);
    printk("TIME"), screen_move_cursor_col(PID_LEN + NAME_LEN + STAT_LEN + CHAN_LEN + TIME_LEN);
    printk("AFF");
    printk("\n");
    int count = 0;
    for (int i = 0; i < NUM_MAX_PCB; i++) {
        pcb_t* proc = pcb_all[i];
        if (proc->status == TASK_EXITED) continue;
        printk("%d", proc->pid);
        screen_move_cursor_col(PID_LEN);
        printk("%s", proc->name);
        screen_move_cursor_col(PID_LEN + NAME_LEN);
        printk("%s", status_str[proc->status]);
        screen_move_cursor_col(PID_LEN + NAME_LEN + STAT_LEN);
        if (proc->list.container) {
            printk("%s", proc->list.container->name);
        } else {
            if (proc->status == TASK_RUNNING) {
                printk("cpu%d", proc->cpu);
            } else {
                printk("N/A");
            }
        }
        screen_move_cursor_col(PID_LEN + NAME_LEN + STAT_LEN + CHAN_LEN);
        printk("%d%%", proc->slice_cnt);
        screen_move_cursor_col(PID_LEN + NAME_LEN + STAT_LEN + CHAN_LEN + TIME_LEN);
        for (int i = 0; i < NR_CPUS; i++) {
            printk("%d", (proc->affinity & (1 << i)) != 0);
        }
        printk("\n");
        count++;
    }
    return count;
}

void do_exit() {
    cleanup_proc(current_running);
    do_scheduler();
}

int do_kill(pid_t pid) {
    pcb_t* pcb = find_pcb(pid);
    if (!pcb || current_running->pid == pid) {
        return 0;
    }
    cleanup_proc(pcb);
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
