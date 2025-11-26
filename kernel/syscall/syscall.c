#include <assert.h>
#include <csr.h>
#include <logger.h>
#include <os/irq.h>
#include <os/kernel.h>
#include <os/lock.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/task.h>
#include <os/time.h>
#include <screen.h>
#include <sys/syscall.h>

long (*syscall[NUM_SYSCALLS])();

void handle_syscall(regs_context_t* regs, uint64_t stval, uint64_t scause) {
    /* TODO: [p2-task3] handle syscall exception */
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
    int sysno = regs->regs[REG_A7];
    int arg0 = regs->regs[REG_A0];
    int arg1 = regs->regs[REG_A1];
    int arg2 = regs->regs[REG_A2];
    int arg3 = regs->regs[REG_A3];
    int arg4 = regs->regs[REG_A4];
    int arg5 = regs->regs[REG_A5];
    // pretty_log(
    //     LOG_INFO, "syscall no: %d, args: %d, %d, %d, %d, %d, %d", sysno, arg0, arg1, arg2, arg3,
    //     arg4, arg5);
    long ret = syscall[sysno](arg0, arg1, arg2, arg3, arg4, arg5);
    regs->regs[REG_A0] = ret;
    regs->sepc += 4;
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

long sys_exec_with_affinity(char* name, int argc, char* argv[], int affinity) {
    task_info_t* task = find_task(name);
    if (!task) {
        pretty_log(LOG_WARN, "exec %s failed: task not found!", name);
        return 0;
    }
    return do_exec(name, task->entrance, argc, argv, affinity);
}

long sys_exec(char* name, int argc, char* argv[]) {
    return sys_exec_with_affinity(name, argc, argv, current_running->affinity);
}

long sys_exec_by_entry(char* name, uint64_t entrance, int argc, char* argv[]) {
    return do_exec(name, entrance, argc, argv, current_running->affinity);
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

long sys_exit(void) {
    do_exit();
    return 0;
}

long sys_kill(pid_t pid) { return do_kill(pid); }

long sys_waitpid(pid_t pid) { return do_waitpid(pid); }

long sys_getpid() { return current_running->pid; }

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
    printk("usage: %s [subcmd ...]\n", argc ? argv[0] : "info");
    for (int i = 0; i < NUM_INFO_COMMANDS; i++) {
        printk("  %s:", INFO_COMMANDS[i].name);
        screen_move_cursor_col(CMD_LEN);
        printk("%s\n", INFO_COMMANDS[i].desc);
    }
}

long sys_display_info(int argc, char** argv) {
    int hit = 0;
    for (int i = 1; i < argc; i++) {
        char* subcmd = argv[i];
        for (int j = 0; j < NUM_INFO_COMMANDS; j++) {
            if (strcmp(subcmd, INFO_COMMANDS[j].name) == 0) {
                INFO_COMMANDS[j].handler(argc, argv);
                hit = 1;
                break;
            }
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

/***************** time *****************/

long sys_get_timebase(void) { return get_time_base(); }

long sys_get_tick(void) { return get_ticks(); }
