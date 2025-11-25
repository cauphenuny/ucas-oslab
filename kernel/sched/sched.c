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
    asserts(0, "pcb not found in pcb_all");
    return -1;
}

void free_pcb(pcb_t* pcb) {
    if (!pcb) return;
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
    size_t size = list_size(list);
    pretty_log(LOG_INFO, "there are %d tasks in the %s/%x channel.", size, list->name, list);
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
        printk("pcb %d: pid=%d, name=%s\n", i, pcb->pid, pcb->name);
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
    asserts(proc, "no process to run");

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
    // TODO: [p2-task3] Check sleep queue to wake up PCBs

    check_sleeping();

    /************************************************************/
    /* Do not touch this comment. Reserved for future projects. */
    /************************************************************/

    // TODO: [p2-task1] Modify the current_running pointer.

    if (current_running->status == TASK_RUNNING) {
        current_running->status = TASK_READY;
        list_append(&ready_queue, &current_running->sched_node);
    }
    // print_all_pcb();
    // print_pcb_list(&ready_queue);
    pcb_t* next_running = pick_process();
    list_delete(&next_running->sched_node);
    // pretty_log(
    //     LOG_INFO, "switch from pid %d(%s) to pid %d(%s).", current_running->pid,
    //     current_running->name, next_running->pid, next_running->name);
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
    do_block(&current_running->sched_node, &sleep_queue);
    do_scheduler();
}

// NOTE: do_block would not delete node from any list
void do_block(list_node_t* pcb_node, list_t* queue) {
    // TODO: [p2-task2] block the pcb task into the block queue
    asserts(!pcb_node->next && !pcb_node->prev, "pcb_node is already in a list");
    pcb_t* pcb = container_of(pcb_node, pcb_t, sched_node);
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
    pcb_t* pcb = container_of(pcb_node, pcb_t, sched_node);
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
            LOG_INFO, "unblocking pid %d from %s", container_of(node, pcb_t, sched_node)->pid,
            name);
        list_delete(node);
        do_unblock(node);
        node = next;
    }
}

void exit_wakeup(pcb_t* pcb) { unblock_list(&pcb->wait_list, "pcb wait_list"); }

void cleanup_proc(pcb_t* pcb) {
    pid_t pid = pcb->pid;
    pretty_log(LOG_INFO, "cleaning up pid %d", pid);
    cleanup_mutexes(pid);
    cleanup_barriers(pid);
    cleanup_conditions(pid);
    cleanup_semaphores(pid);
    cleanup_mailboxes(pid);
    list_node_destruct(&pcb->sched_node);
    list_node_destruct(&pcb->relation_node);
    exit_wakeup(pcb);
    free_pcb(pcb);
}

void attach_subprocess(pcb_t* parent, pcb_t* child) {
    list_append(&parent->child_list, &child->relation_node);
    child->parent = parent;
}

pid_t do_exec(const char* name, uint64_t entrance, int argc, char* argv[], unsigned affinity_mask) {
    pretty_log(LOG_DEBUG, "handling exec for %s", name);
    pcb_t* pcb = construct_pcb(name, entrance, argc, argv, 2, 8);
    if (!pcb) {
        pretty_log(LOG_WARN, "exec %s failed: failed to allocate pcb!", name);
        return 0;
    }
    pretty_log(LOG_INFO, "exec %s succeeded! pid=%d", name, pcb->pid);
    set_proc_affinity(pcb, affinity_mask);
    list_append(&ready_queue, &pcb->sched_node);
    attach_subprocess(current_running, pcb);
    log_all_pcb();
    return pcb->pid;
}

int do_process_show() {
    const int PID_LEN = 5, PID_SUM = PID_LEN;
    const int PPID_LEN = 6, PPID_SUM = PID_SUM + PPID_LEN;
    const int NAME_LEN = 16, NAME_SUM = PPID_SUM + NAME_LEN;
    const int STAT_LEN = 10, STAT_SUM = NAME_SUM + STAT_LEN;
    const int CHAN_LEN = 9, CHAN_SUM = STAT_SUM + CHAN_LEN;
    const int TIME_LEN = 6, TIME_SUM = CHAN_SUM + TIME_LEN;
    const int AFF_LEN = NR_CPUS + 3, AFF_SUM = TIME_SUM + AFF_LEN;
    const int MEM_LEN = 7, MEM_SUM = AFF_SUM + MEM_LEN;
    const int UMEM_LEN = 7, UMEM_SUM = MEM_SUM + UMEM_LEN;
    const char* status_str[] = {
        [TASK_BLOCKED] = "BLOCKED",
        [TASK_READY] = "READY",
        [TASK_RUNNING] = "RUNNING",
        [TASK_EXITED] = "EXITED",
    };
    printkf("PID"), screen_move_cursor_col(PID_SUM);
    printkf("PPID"), screen_move_cursor_col(PPID_SUM);
    printkf("NAME"), screen_move_cursor_col(NAME_SUM);
    printkf("STATUS"), screen_move_cursor_col(STAT_SUM);
    printkf("CHANNEL"), screen_move_cursor_col(CHAN_SUM);
    printkf("CPU"), screen_move_cursor_col(TIME_SUM);
    printkf("AFF"), screen_move_cursor_col(AFF_SUM);
    printkf("MEM/K"), screen_move_cursor_col(MEM_SUM);
    printkf("MEM/U"), screen_move_cursor_col(UMEM_SUM);
    printkf("\n");
    int count = 0;
    for (int i = 0; i < NUM_MAX_PCB; i++) {
        pcb_t* proc = pcb_all[i];
        if (proc->status == TASK_EXITED) continue;
        printkf("%d", proc->pid);
        screen_move_cursor_col(PID_SUM);
        if (proc->parent) {
            printkf("%d", proc->parent->pid);
        } else {
            printkf("N/A");
        }
        screen_move_cursor_col(PPID_SUM);
        printkf("%s", proc->name);
        screen_move_cursor_col(NAME_SUM);
        printkf("%s", status_str[proc->status]);
        screen_move_cursor_col(STAT_SUM);
        if (proc->sched_node.container) {
            printkf("%s", proc->sched_node.container->name);
        } else {
            if (proc->status == TASK_RUNNING) {
                printkf("cpu%d", proc->cpu);
            } else {
                printkf("N/A");
            }
        }
        screen_move_cursor_col(CHAN_SUM);
        printkf("%d%%", proc->slice_cnt);
        screen_move_cursor_col(TIME_SUM);
        for (int i = 0; i < NR_CPUS; i++) {
            printkf("%d", (proc->affinity & (1 << i)) != 0);
        }
        screen_move_cursor_col(AFF_SUM);
        printkf("%d", proc->kernel_stack_top - proc->kernel_sp);
        screen_move_cursor_col(MEM_SUM);
        if (proc->pid >= NR_CPUS) {
            printkf("%d", proc->user_stack_top - proc->user_sp);
        } else {
            printkf("N/A");
        }
        screen_move_cursor_col(UMEM_SUM);
        printkf("\n");
        count++;
    }
    screen_reflush();
    return count;
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
    printk("%s (pid=%d)\n", pcb->name, pcb->pid);
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

void do_exit() {
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
    for (list_node_t* iter = pcb->child_list.head.next; iter != &pcb->child_list.head;) {
        asserts(iter, "iterator is NULL");
        list_node_t* next = iter->next;
        pcb_t* child = container_of(iter, pcb_t, relation_node);
        pretty_log(LOG_INFO, "killing child pid %d of pid %d", child->pid, pid);
        do_kill(child->pid);
        iter = next;
    }
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
