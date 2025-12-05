#include <asm/unistd.h>
#include <assert.h>
#include <csr.h>
#include <logger.h>
#include <os/irq.h>
#include <os/kernel.h>
#include <os/lock.h>
#include <os/mm.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/task.h>
#include <os/time.h>
#include <screen.h>
#include <sys/syscall.h>

long (*syscall[NUM_SYSCALLS])();

void handle_syscall(regs_context_t* regs, uint64_t stval, uint64_t scause) {
    /* DONE: [p2-task3] handle syscall exception */
    /**
     * HINT: call syscall function like syscall[fn](arg0, arg1, arg2),
     * and pay attention to the return value and sepc
     */
    int is_irq = (scause & SCAUSE_IRQ_FLAG) != 0;
    assert(!is_irq);
    uint64_t exception_code = scause & (~SCAUSE_IRQ_FLAG);
    if (exception_code == 8) {
        // pretty_log(LOG_INFO, "handling ecall from U-mode");
    } else if (exception_code == 9) {
        // pretty_log(LOG_INFO, "handling ecall from S-mode");
    } else {
        assert(false);
    }
    reg_t sysno = regs->regs[REG_A7];
    reg_t arg0 = regs->regs[REG_A0];
    reg_t arg1 = regs->regs[REG_A1];
    reg_t arg2 = regs->regs[REG_A2];
    reg_t arg3 = regs->regs[REG_A3];
    reg_t arg4 = regs->regs[REG_A4];
    reg_t arg5 = regs->regs[REG_A5];
    // pretty_log(
    //     LOG_INFO, "syscall no: %d, args: %d, %d, %d, %d, %d, %d", sysno, arg0, arg1, arg2, arg3,
    //     arg4, arg5);
    regs->sepc += 4;
    reg_t ret = syscall[sysno](arg0, arg1, arg2, arg3, arg4, arg5);
    regs->regs[REG_A0] = ret;
}

/***************** proc *****************/

long sys_sleep(uint32_t time) {
    do_sleep(time);
    return 0;
}

long sys_msleep(uint32_t msec) {
    uint32_t sleep_time = (msec + time_base - 1) / time_base;
    do_sleep(sleep_time);
    return 0;
}

long sys_yield(void) {
    do_scheduler();
    return 0;
}

long exec_dispatch(char* name, int argc, char* argv[], uint64_t entrance, int affinity) {
    task_info_t* task = find_task(name);
    if (!task) {
        pretty_log(LOG_WARN, "exec %s failed: task not found!", name);
        return 0;
    }
    if (entrance == -1) {
        entrance = task->entrance;
    }
    return do_exec(task, entrance, argc, argv, affinity);
}

long sys_exec_with_affinity(char* name, int argc, char* argv[], int affinity) {
    return exec_dispatch(name, argc, argv, -1, affinity);
}

long sys_exec(char* name, int argc, char* argv[]) {
    return exec_dispatch(name, argc, argv, -1, current_running->affinity);
}

long sys_exec_by_entry(char* name, uint64_t entrance, int argc, char* argv[]) {
    return exec_dispatch(name, argc, argv, entrance, current_running->affinity);
}

long sys_set_affinity(int pid, unsigned affinity_mask) {
    pcb_t* pcb = find_pcb(pid);
    if (pcb) {
        int err = set_proc_affinity(pcb, affinity_mask);
        return !err;
    }
    return 0;
}

long sys_set_workload(int workload) {
    set_process_workload(workload);
    return 0;
}

long sys_set_nice(int nice, int pid) { return set_process_nice(nice, pid); }

long sys_exit(void) {
    do_exit();
    return 0;
}

long sys_kill(pid_t pid) { return do_kill(pid); }

long sys_waitpid(pid_t pid) { return do_waitpid(pid); }

long sys_getpid() { return current_running->pid; }

long sys_get_free_memory() { return get_free_memory(); }

long sys_set_max_memory(size_t max_mem) {
    pageframe_group_t* group = find_pagegroup(current_running->pgdir);
    if (group == PAGE_GROUP_KERNEL) {
        return fork_pagegroup(current_running->pgdir, max_mem / PAGE_SIZE, current_running->name);
    }
    return resize_pagegroup(group, max_mem / PAGE_SIZE);
}

long sys_set_page_repl_algo(const char* algo) {
    pageframe_group_t* group = find_pagegroup(current_running->pgdir);
    int ret = 0;
    pretty_logi("try set group '%s' replacement algorithm to %s", group->pages.name, algo);
    if (strcmp(algo, "lru") == 0) {
        group->maintain = maintain_pagelist_lru;
    } else if (strcmp(algo, "fifo") == 0) {
        group->maintain = maintain_pagelist_fifo;
    } else {
        ret = 1;
    }
    return ret;
}

long sys_process_show() { return do_process_show(); }

long sys_task_show() {
    show_tasks();
    return 0;
}

void show_sync();
void show_time();
void show_help(int argc, char** argv);

const struct {
    const char* name;
    const char* desc;
    void (*handler)(int argc, char** argv);
} INFO_COMMANDS[] = {
    {"task", "display runnable tasks", show_tasks},
    {"proc", "display current processes", (void (*)())do_process_show},
    {"ptree", "display process tree", show_process_tree},
    {"pcb", "display pcb array", show_pcb},
    {"time", "display timer and cputime", show_time},
    {"cond", "display condition status", show_conditions},
    {"mutex", "display mutex status", show_mutexes},
    {"bar", "display barrier status", show_barriers},
    {"sema", "display semaphore status", show_semaphores},
    {"sync", "display all synchronization machanics", show_sync},
    {"mbox", "display mailbox status", show_mailboxes},
    {"page", "display page frame group status", show_pagegroups},
    {"swap", "display swap status", show_swap},
    {"help", "display this help message", show_help},
};

const int NUM_INFO_COMMANDS = sizeof(INFO_COMMANDS) / sizeof(INFO_COMMANDS[0]);

void show_sync() {
    show_mutexes();
    show_conditions();
    show_barriers();
    show_semaphores();
    show_mailboxes();
}

void show_time() {
    show_timer();
    show_cputime();
}

void show_help(int argc, char** argv) {
    const int CMD_LEN = 13;
    printk("usage: info [subcmd ...]\n");
    for (int i = 0; i < NUM_INFO_COMMANDS; i++) {
        printk("  %s:", INFO_COMMANDS[i].name);
        screen_move_cursor_col(CMD_LEN);
        printk("%s\n", INFO_COMMANDS[i].desc);
    }
}

long sys_display_info(int argc, char** argv) {
    int hit = 0;
    char* subcmd = argv[1];
    for (int j = 0; j < NUM_INFO_COMMANDS; j++) {
        if (strcmp(subcmd, INFO_COMMANDS[j].name) == 0) {
            INFO_COMMANDS[j].handler(argc - 1, argv + 1);
            hit = 1;
            break;
        }
    }
    if (!hit) {
        show_help(argc, argv);
    }
    return !hit;
}

long sys_screen_set_scroll(int start_row, int end_row) {
    screen_set_scroll(start_row, end_row);
    return 0;
}

long sys_screen_clear_scroll(void) {
    screen_clear_scroll();
    return 0;
}

long sys_screen_set_color(int start_col, int end_col, int foreground, int background) {
    screen_set_color(start_col, end_col, foreground, background);
    return 0;
}

long sys_screen_clear_color(void) {
    screen_clear_color();
    return 0;
}

long sys_screen_delete_line(int nlines) {
    screen_delete_line(nlines);
    return 0;
}

/***************** sync *****************/

long sys_lock_init(int key) { return do_mutex_lock_init(key); }

long sys_lock_acquire(int handle) {
    do_mutex_lock_acquire(handle);
    return 0;
}

long sys_lock_release(int handle) {
    do_mutex_lock_release(handle);
    return 0;
}

long sys_barrier_init(int key, int goal) { return do_barrier_init(key, goal); }

long sys_barrier_destroy(int bar_idx) {
    do_barrier_destroy(bar_idx);
    return 0;
}

long sys_barrier_wait(int bar_idx) {
    do_barrier_wait(bar_idx);
    return 0;
}

long sys_condition_init(int key) { return do_condition_init(key); }

long sys_condition_wait(int cond_idx, int mutex_idx) {
    do_condition_wait(cond_idx, mutex_idx);
    return 0;
}

long sys_condition_signal(int cond_idx) {
    do_condition_signal(cond_idx);
    return 0;
}

long sys_condition_broadcast(int cond_idx) {
    do_condition_broadcast(cond_idx);
    return 0;
}

long sys_condition_destroy(int cond_idx) {
    do_condition_destroy(cond_idx);
    return 0;
}

long sys_semaphore_init(int key, int init) { return do_semaphore_init(key, init); }

long sys_semaphore_up(int sema_idx) {
    do_semaphore_up(sema_idx);
    return 0;
}

long sys_semaphore_down(int sema_idx) {
    do_semaphore_down(sema_idx);
    return 0;
}

long sys_semaphore_destroy(int sema_idx) {
    do_semaphore_destroy(sema_idx);
    return 0;
}

long sys_mbox_open(char* name) { return do_mbox_open(name); }

long sys_mbox_close(int mbox_id) {
    do_mbox_close(mbox_id);
    return 0;
}

long sys_mbox_send(int mbox_idx, void* msg, int msg_length) {
    return do_mbox_send(mbox_idx, msg, msg_length);
}

long sys_mbox_recv(int mbox_idx, void* msg, int msg_length) {
    return do_mbox_recv(mbox_idx, msg, msg_length);
}

/***************** screen *****************/

long sys_write(char* buff) {
    screen_write(buff);
    return 0;
}

long sys_readch(void) { return bios_getchar(); }

long sys_move_cursor(int x, int y) {
    screen_move_cursor(x, y);
    return 0;
}

long sys_move_cursor_row(int row) {
    screen_move_cursor_row(row);
    return 0;
}

long sys_move_cursor_col(int col) {
    screen_move_cursor_col(col);
    return 0;
}

long sys_screen_reflush(void) {
    screen_reflush();
    return 0;
}

long sys_screen_clear(void) {
    screen_clear();
    return 0;
}

long sys_screen_clear_lines(int start, int end) {
    screen_clear_lines(start, end);
    return 0;
}

/***************** time *****************/

long sys_get_timebase(void) { return get_time_base(); }

long sys_get_tick(void) { return get_ticks(); }

long sys_get_proc_tick(void) { return get_proc_tick(); }

/***************** pipe *****************/

long sys_pipe_open(const char* name) { return pipe_open(name); }
long sys_pipe_give_pages(int idx, void *src, size_t length) {
    return pipe_give_pages(idx, (kva_t)src, length);
}
long sys_pipe_take_pages(int idx, void *dest, size_t length) {
    return pipe_take_pages(idx, (kva_t)dest, length);
}

/***************** set handler *****************/

void init_syscall(void) {
    // DONE: [p2-task3] initialize system call table.
    syscall[SYSCALL_EXEC] = sys_exec;
    syscall[SYSCALL_EXIT] = sys_exit;
    syscall[SYSCALL_EXEC_WITH_AFF] = sys_exec_with_affinity;
    syscall[SYSCALL_EXEC_BY_ENTRY] = sys_exec_by_entry;
    syscall[SYSCALL_SLEEP] = sys_sleep;
    syscall[SYSCALL_KILL] = sys_kill;
    syscall[SYSCALL_WAITPID] = sys_waitpid;
    syscall[SYSCALL_GETPID] = sys_getpid;
    syscall[SYSCALL_YIELD] = sys_yield;

    syscall[SYSCALL_PS] = sys_process_show;
    syscall[SYSCALL_TASK_SHOW] = sys_task_show;
    syscall[SYSCALL_DISPLAY_INFO] = sys_display_info;

    syscall[SYSCALL_FREE_MEM] = sys_get_free_memory;
    syscall[SYSCALL_SET_MAX_MEM] = sys_set_max_memory;
    syscall[SYSCALL_SET_PAGE_ALGO] = sys_set_page_repl_algo;

    syscall[SYSCALL_WRITE] = sys_write;
    syscall[SYSCALL_READCH] = sys_readch;
    syscall[SYSCALL_CURSOR] = sys_move_cursor;
    syscall[SYSCALL_CURSOR_COL] = sys_move_cursor_col;
    syscall[SYSCALL_CURSOR_ROW] = sys_move_cursor_row;
    syscall[SYSCALL_REFLUSH] = sys_screen_reflush;
    syscall[SYSCALL_CLEAR] = sys_screen_clear;

    syscall[SYSCALL_GET_TIMEBASE] = sys_get_timebase;
    syscall[SYSCALL_GET_TICK] = sys_get_tick;
    syscall[SYSCALL_GET_PROC_TICK] = sys_get_proc_tick;

    syscall[SYSCALL_LOCK_INIT] = sys_lock_init;
    syscall[SYSCALL_LOCK_ACQ] = sys_lock_acquire;
    syscall[SYSCALL_LOCK_RELEASE] = sys_lock_release;

    syscall[SYSCALL_BARR_INIT] = sys_barrier_init;
    syscall[SYSCALL_BARR_WAIT] = sys_barrier_wait;
    syscall[SYSCALL_BARR_DESTROY] = sys_barrier_destroy;

    syscall[SYSCALL_COND_INIT] = sys_condition_init;
    syscall[SYSCALL_COND_WAIT] = sys_condition_wait;
    syscall[SYSCALL_COND_SIGNAL] = sys_condition_signal;
    syscall[SYSCALL_COND_BROADCAST] = sys_condition_broadcast;
    syscall[SYSCALL_COND_DESTROY] = sys_condition_destroy;

    syscall[SYSCALL_SEMA_INIT] = sys_semaphore_init;
    syscall[SYSCALL_SEMA_UP] = sys_semaphore_up;
    syscall[SYSCALL_SEMA_DOWN] = sys_semaphore_down;
    syscall[SYSCALL_SEMA_DESTROY] = sys_semaphore_destroy;

    syscall[SYSCALL_MBOX_OPEN] = sys_mbox_open;
    syscall[SYSCALL_MBOX_CLOSE] = sys_mbox_close;
    syscall[SYSCALL_MBOX_SEND] = sys_mbox_send;
    syscall[SYSCALL_MBOX_RECV] = sys_mbox_recv;

    syscall[SYSCALL_PIPE_OPEN] = sys_pipe_open;
    syscall[SYSCALL_PIPE_GIVE] = sys_pipe_give_pages;
    syscall[SYSCALL_PIPE_TAKE] = sys_pipe_take_pages;

    syscall[SYSCALL_SET_WORKLOAD] = sys_set_workload;
    syscall[SYSCALL_SET_AFFINITY] = sys_set_affinity;
    syscall[SYSCALL_SET_NICE] = sys_set_nice;

    syscall[SYSCALL_SET_SCROLL] = sys_screen_set_scroll;
    syscall[SYSCALL_CLEAR_SCROLL] = sys_screen_clear_scroll;
    syscall[SYSCALL_SET_COLOR] = sys_screen_set_color;
    syscall[SYSCALL_CLEAR_COLOR] = sys_screen_clear_color;
    syscall[SYSCALL_DELETE_LINE] = sys_screen_delete_line;
    syscall[SYSCALL_CLEAR_LINE] = sys_screen_clear_lines;
}
