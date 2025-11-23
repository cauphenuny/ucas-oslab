#include <os/kernel.h>
#include <os/task.h>
#include <os/lock.h>
#include <os/sched.h>
#include <os/time.h>
#include <screen.h>
#include <assert.h>
#include <csr.h>
#include <logger.h>
#include <sys/syscall.h>

long (*syscall[NUM_SYSCALLS])();

void handle_syscall(regs_context_t* regs, uint64_t stval, uint64_t scause) {
    /* TODO: [p2-task3] handle syscall exception */
    /**
     * HINT: call syscall function like syscall[fn](arg0, arg1, arg2),
     * and pay attention to the return value and sepc
     */
    int is_irq = (scause & SCAUSE_IRQ_FLAG) == 1ull;
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
    //     LOG_INFO, "syscall no: %d, args: %d, %d, %d, %d, %d, %d", sysno, arg0, arg1, arg2, arg3, arg4,
    //     arg5);
    long ret = syscall[sysno](arg0, arg1, arg2, arg3, arg4, arg5);
    regs->regs[REG_A0] = ret;
    regs->regs[REG_SEPC] += 4;
}

/***************** proc *****************/

long sys_sleep(uint32_t time) {
    do_sleep(time);
    return 0;
}

long sys_yield(void) {
    do_scheduler();
    return 0;
}

long sys_exec(char *name, int argc, char *argv[]) {
    return do_exec(name, argc, argv);
}

long sys_set_workload(int workload) {
    set_process_workload(workload);
    return 0;
}

long sys_exit(void) {
    do_exit();
    return 0;
}

long sys_kill(pid_t pid) {
    return do_kill(pid);
}

long sys_waitpid(pid_t pid) {
    return do_waitpid(pid);
}

long sys_getpid() {
    return current_running->pid;
}

long sys_process_show() {
    do_process_show();
    return 0;
}

long sys_task_show() {
    show_tasks();
    return 0;
}

/***************** sync *****************/

long sys_lock_init(int key) {
    return do_mutex_lock_init(key);
}

long sys_lock_acquire(int handle) {
    do_mutex_lock_acquire(handle);
    return 0;
}

long sys_lock_release(int handle) {
    do_mutex_lock_release(handle);
    return 0;
}

long sys_barrier_init(int key, int goal) {
    return do_barrier_init(key, goal);
}

long sys_barrier_destroy(int bar_idx) {
    do_barrier_destroy(bar_idx);
    return 0;
}

long sys_barrier_wait(int bar_idx) {
    do_barrier_wait(bar_idx);
    return 0;
}

long sys_condition_init(int key) {
    return do_condition_init(key);
}

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

long sys_semaphore_init(int key, int init) {
    return do_semaphore_init(key, init);
}

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

long sys_mbox_open(char *name) {
    return do_mbox_open(name);
}

long sys_mbox_close(int mbox_id) {
    do_mbox_close(mbox_id);
    return 0;
}

long sys_mbox_send(int mbox_idx, void *msg, int msg_length) {
    return do_mbox_send(mbox_idx, msg, msg_length);
}

long sys_mbox_recv(int mbox_idx, void *msg, int msg_length) {
    return do_mbox_recv(mbox_idx, msg, msg_length);
}

/***************** screen *****************/

long sys_write(char *buff) {
    screen_write(buff);
    return 0;
}

long sys_readch(void) {
    return bios_getchar();
}

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

long sys_get_timebase(void) {
    return get_time_base();
}

long sys_get_tick(void) {
    return get_ticks();
}

