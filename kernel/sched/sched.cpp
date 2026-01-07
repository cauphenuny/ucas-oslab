#include "tui.hpp"

extern "C" {

#include <asm/regs.h>
#include <assert.h>
#include <breakpoint.h>
#include <logger.h>
#include <os/list.h>
#include <os/lock.h>
#include <os/mm.h>
#include <os/net.h>
#include <os/sched.h>
#include <os/smp.h>
#include <os/string.h>
#include <os/task.h>
#include <os/time.h>
#include <pgtable.h>
#include <printk.h>
#include <screen.h>

pcb_t pcb_kernel[NR_CPUS];
pcb_t pcb_user[NUM_MAX_TASK];
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
    if (pid < 0) return NULL;
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

int get_pcb_index(pid_t pid) {
    pcb_t* pcb = find_pcb(pid);
    asserts(pcb, "cannot find pcb in get_pcb_index");
    for (int i = 0; i < NUM_MAX_PCB; i++) {
        if (pcb_all[i] == pcb) {
            return i;
        }
    }
    err_halt("pcb not found in pcb_all");
    return -1;
}

void free_pcb(pcb_t* pcb) {
    if (!pcb) return;
    asserts(pcb->pid >= NR_CPUS, "cannot free kernel pcb");
    pcb->status = TASK_EXITED;
}

LIST(ready_queue, "ready");
LIST(sleep_queue, "sleep");

/* global process id */
pid_t process_id = NR_CPUS;

void log_pcb_array(const pcb_t pcb[], int n) {
    for (int i = 0; i < n; i++) {
        if (pcb[i].status == TASK_EXITED) continue;
        ptr_t kernel_ra, user_ra;
        fetch_pcb_info(&pcb[i], &kernel_ra, &user_ra);
        pretty_log(
            LOG_DEBUG, "pid=%d, name=%s, aff=0x%x, stat=%d, chan=%s, kctx=%x/%x, uctx=%x/%x",
            pcb[i].pid, pcb[i].name, pcb[i].affinity, pcb[i].status,
            pcb[i].sched_node.container ? pcb[i].sched_node.container->name : "NULL", kernel_ra,
            pcb[i].kernel_sp, user_ra, pcb[i].user_sp);
    }
}

void log_pcb_list(const list_t* list) {
    pretty_log(
        LOG_INFO, "there are %d tasks in the %s/%x channel.", list_size(list), list->name, list);
    list_node_t* current = list->head.next;
    while (current != &list->head) {
        pcb_t* pcb = container_of(current, pcb_t, sched_node);
        ptr_t kernel_ra, user_ra;
        fetch_pcb_info(pcb, &kernel_ra, &user_ra);
        pretty_log(
            LOG_DEBUG, "pid=%d, name=%s, stat=%d, chan=%s, kctx=%x/%x, uctx=%x/%x", pcb->pid,
            pcb->name, pcb->status,
            pcb->sched_node.container ? pcb->sched_node.container->name : "NULL", kernel_ra,
            pcb->kernel_sp, user_ra, pcb->user_sp);
        current = current->next;
    }
}

void log_all_pcb() {
    log_pcb_array(pcb_kernel, NR_CPUS);
    log_pcb_array(pcb_user, NUM_MAX_TASK);
}

void show_pcb() {
    for (int i = 0; i < NUM_MAX_PCB; i++) {
        pcb_t* pcb = pcb_all[i];
        if (pcb->status == TASK_EXITED) continue;
        printk("pcb %d: pid=%d, name=%s, status=%d\n", i, pcb->pid, pcb->name, pcb->status);
    }
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
    int normalized_cnt = proc->slice_cnt / (proc->task_workload + 1) * (10 + proc->nice) / 10;
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
        pcb_t* next = container_of(iter, pcb_t, sched_node);
        if (!filter(next)) continue;
        if (update(selected, next)) {
            selected = next;
        }
    }
    return selected;
}

pcb_t* pick_process() {
    pcb_t* proc = NULL;
    proc = pick_process_impl(
        &ready_queue, filterout_kernel, update_by_consumption_init, update_by_consumption);
    if (!proc) {
        // pretty_log(LOG_WARN, "process insufficient, may fallback to init");
        proc = pick_process_impl(&ready_queue, filter_affinity, NULL, update_by_first);
    }
    if (!proc) {
        do_process_show();
        show_pcb();
        err_halt("no process to run");
    }

    // pretty_log(
    //     LOG_DEBUG, "selected pid %d (task_id=%d, workload=%d, slice_cnt=%d)", proc->pid,
    //     proc->task_id, proc->task_workload, proc->slice_cnt);
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
    // DONE: [p2-task3] Check sleep queue to wake up PCBs

    check_sleeping();

    /************************************************************/

    // DONE: [p5-task3] Check send/recv queue to unblock PCBs (by interrupt)

    /************************************************************/

    // DONE: [p2-task1] Modify the current_running pointer.

    if (current_running->status == TASK_RUNNING) {
        current_running->status = TASK_READY;
        list_append(&ready_queue, &current_running->sched_node);
    }
    // print_all_pcb();
    // print_pcb_list(&ready_queue);
    pcb_t* next_running = pick_process();
    list_delete(&next_running->sched_node);
    // pretty_logi(
    //     "switch from pid %d(%s) to pid %d(%s).", current_running->pid, current_running->name,
    //     next_running->pid, next_running->name);
    next_running->status = TASK_RUNNING;

    set_satp(SATP_MODE_SV39, next_running->pid, kva2pa(next_running->pgdir) >> NORMAL_PAGE_SHIFT);
    local_flush_tlb_all();
    // pretty_log(
    //     LOG_INFO, "satp set to pid %d pgdir 0x%x", next_running->pid,
    //     kva2pa(next_running->pgdir));

    // DONE: [p2-task1] switch_to current_running
    unlock_kernel();
    switch_to(current_running, next_running);
    lock_kernel(NULL, 0, 0, 0);

    screen_move_cursor(current_running->cursor_x, current_running->cursor_y);
    current_running->cpu = get_current_cpu_id();

    // breakpoint();
}

void do_sleep(uint32_t sleep_time) {
    // DONE: [p2-task3] sleep(seconds)
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
    do_block(&current_running->sched_node, &sleep_queue);
    do_scheduler();
}

// NOTE: do_block would not delete node from any list
void do_block(list_node_t* pcb_node, list_t* queue) {
    // DONE: [p2-task2] block the pcb task into the block queue
    pcb_t* pcb = container_of(pcb_node, pcb_t, sched_node);
    // pretty_log(LOG_INFO, "blocking pid %d(status=%d)", pcb->pid, pcb->status);
    if (pcb->status == TASK_BLOCKED) {
        pretty_loge("double blocking a task(name=%s, pid=%d)", pcb->name, pcb->pid);
        return;
    }
    asserts(!pcb_node->next && !pcb_node->prev, "pcb_node is already in a list");
    pcb->status = TASK_BLOCKED;
    list_append(queue, pcb_node);
}

/**
 * @brief unblock the `pcb` to ready queue
 */
void do_unblock(list_node_t* pcb_node) {
    // DONE: [p2-task2] unblock the `pcb` from the block queue
    pcb_t* pcb = container_of(pcb_node, pcb_t, sched_node);
    if (pcb->status != TASK_BLOCKED) {
        pretty_log(
            LOG_WARN, "unblocking a non-blocked task(pid=%d, status=%d)", pcb->pid, pcb->status);
    }
    pcb->status = TASK_READY;
    // list_delete(pcb_node);
    list_append(&ready_queue, pcb_node);
}

void unblock_list(list_t* queue) {
    list_node_t* node = queue->head.next;
    while (node != &queue->head) {
        list_node_t* next = node->next;
        // pretty_log(
        //     LOG_INFO, "unblocking pid %d from %s", container_of(node, pcb_t, sched_node)->pid,
        //     queue->name);
        list_delete(node);
        do_unblock(node);
        node = next;
    }
}

void exit_wakeup(pcb_t* pcb) { unblock_list(&pcb->wait_list); }

void cleanup_proc(pcb_t* pcb) {
    pid_t pid = pcb->pid;
    pretty_log(LOG_INFO, "cleaning pid %d", pid);
    cleanup_mutexes(pid);
    cleanup_barriers(pid);
    cleanup_conditions(pid);
    cleanup_semaphores(pid);
    cleanup_mailboxes(pid);
    cleanup_pipe(pid);
    list_node_destruct(&pcb->sched_node);
    list_node_destruct(&pcb->relation_node);
    cleanup_vm(pcb);
    exit_wakeup(pcb);
    free_pcb(pcb);
    pretty_logi("pid %d cleaned", pid);
}

void attach_subprocess(pcb_t* parent, pcb_t* child) {
    list_append(&parent->child_list, &child->relation_node);
    child->parent = parent;
}

pid_t do_exec(
    const task_info_t* task, uint64_t entrance, int argc, char* argv[], unsigned affinity_mask) {
    pretty_log(LOG_DEBUG, "handling exec for %s", task->name);
    pcb_t* pcb = construct_pcb(task, entrance, argc, argv, 2, 8);
    if (!pcb) {
        pretty_log(LOG_WARN, "exec %s failed: failed to allocate pcb!", task->name);
        return 0;
    }
    pretty_log(LOG_INFO, "exec %s succeeded! pid=%d", task->name, pcb->pid);
    set_proc_affinity(pcb, affinity_mask);
    list_append(&ready_queue, &pcb->sched_node);
    attach_subprocess(current_running, pcb);
    log_all_pcb();
    return pcb->pid;
}

int do_process_show() {
    const char* status_str[TASK_STATUS_SIZE] = {"BLOCKED", "RUNNING", "READY", "EXITED", "KILLED"};

    using T = pcb_t*;
    return display_table<T>(
        pcb_all, NUM_MAX_PCB, [](const T* proc) { return (*proc)->status != TASK_EXITED; },
        table_entry_t{"PID", 5, [](const T* proc) { printkf("%d", (*proc)->pid); }},
        table_entry_t{
            "PPID", 6,
            [](const T* proc) {
                if ((*proc)->parent) {
                    printkf("%d", (*proc)->parent->pid);
                } else {
                    printkf("N/A");
                }
            }},
        table_entry_t{"COMMAND", 18, [](const T* proc) { printkf("%s", (*proc)->cmd); }},
        table_entry_t{
            "STATUS", 10,
            [&status_str](const T* proc) { printkf("%s", status_str[(*proc)->status]); }},
        table_entry_t{
            "CHANNEL", 10,
            [](const T* proc) {
                if ((*proc)->sched_node.container) {
                    printkf("%s", (*proc)->sched_node.container->name);
                } else {
                    if ((*proc)->status == TASK_RUNNING) {
                        printkf("cpu%d", (*proc)->cpu);
                    } else {
                        printkf("N/A");
                    }
                }
            }},
        table_entry_t{"CPU", 6, [](const T* proc) { printkf("%d%%", (*proc)->slice_cnt); }},
        table_entry_t{
            "AFF", NR_CPUS + 3,
            [](const T* proc) {
                for (int i = 0; i < NR_CPUS; i++) {
                    printkf("%d", (((*proc)->affinity) & (1 << i)) != 0);
                }
            }},
        table_entry_t{
            "MEM/K", 7,
            [](const T* proc) { printkf("%d", (*proc)->kernel_stack_base - (*proc)->kernel_sp); }},
        table_entry_t{
            "MEM/U", 7,
            [](const T* proc) {
                if ((*proc)->pid >= NR_CPUS) {
                    printkf("%d", (*proc)->user_stack_base - (*proc)->user_sp);
                } else {
                    printkf("N/A");
                }
            }},
        table_entry_t{"NI", 4, [](const T* proc) { printkf("%d", (*proc)->nice); }});
}

int have_next[NUM_MAX_TASK];

void dfs(int depth, pcb_t* pcb) {
    if (!pcb || pcb->status == TASK_EXITED) {
        return;
    }
    for (int i = 0; i < depth - 1; i++) {
        if (have_next[i]) {
            printk("|   ");
        } else {
            printk("    ");
        }
    }
    if (depth > 0) {
        printk("|-> ");
    }
    printk("%s (pid=%d)\n", pcb->cmd, pcb->pid);
    list_foreach_node(iter, &pcb->child_list.head) {
        pcb_t* child = container_of(iter, pcb_t, relation_node);
        if (iter->next != &pcb->child_list.head) {
            have_next[depth] = 1;
        } else {
            have_next[depth] = 0;
        }
        dfs(depth + 1, child);
    }
}

void show_process_tree() {
    memset(have_next, 0, sizeof(have_next));
    for (int i = 0; i < NUM_MAX_PCB; i++) {
        pcb_t* pcb = pcb_all[i];
        if (!pcb || pcb->status == TASK_EXITED) {
            continue;
        }
        pcb_t* parent = pcb->parent;
        if (parent && parent->status != TASK_EXITED) {
            continue;
        }
        dfs(0, pcb);
    }
}

static void kill_subprocess(pcb_t* pcb) {
    for (list_node_t* iter = pcb->child_list.head.next; iter != &pcb->child_list.head;) {
        asserts(iter, "iterator is NULL");
        list_node_t* next = iter->next;
        pcb_t* child = container_of(iter, pcb_t, relation_node);
        pretty_log(LOG_INFO, "killing child pid %d of pid %d", child->pid, pcb->pid);
        do_kill(child->pid);
        iter = next;
    }
}

void do_exit() {
    asserts(current_running->pid >= NR_CPUS, "kernel process cannot exit");

    kill_subprocess(current_running);

    use_kernel_satp();  // use kernel satp before cleaning up
    cleanup_proc(current_running);
    do_scheduler();
}

int do_kill(pid_t pid) {
    if (pid < NR_CPUS) {
        pretty_log(LOG_WARN, "cannot kill kernel process(pid=%d)", pid);
        return 0;
    }
    pcb_t* parent = current_running;
    while (parent) {
        if (parent->pid == pid) {
            pretty_log(LOG_WARN, "cannot kill self or ancestor(pid=%d)", pid);
            return 0;
        }
        parent = parent->parent;
    }
    pcb_t* pcb = find_pcb(pid);
    if (!pcb) {
        pretty_log(LOG_WARN, "cannot find process(pid=%d) to kill", pid);
        return 0;
    }
    if (pcb->status == TASK_RUNNING) {
        pretty_log(LOG_INFO, "process(pid=%d) is running, mark as killed", pid);
        pcb->status = TASK_KILLED;
        return 1;
    }
    kill_subprocess(pcb);
    cleanup_proc(pcb);
    pretty_log(LOG_INFO, "killed process(pid=%d) successfully", pid);
    return 1;
}

int do_waitpid(pid_t pid) {
    pcb_t* pcb = find_pcb(pid);
    if (!pcb) {
        return 0;
    }
    list_append(&pcb->wait_list, &current_running->sched_node);
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

int set_process_nice(int nice, int pid) {
    if (nice < 0) {
        pretty_log(LOG_WARN, "nice value %d out of range!", nice);
        return -1;
    }
    pcb_t* pcb = find_pcb(pid);
    if (!pcb) {
        pretty_log(LOG_WARN, "cannot find process(pid=%d)", pid);
        return -1;
    }
    pcb->nice = nice;
    return 0;
}
}
