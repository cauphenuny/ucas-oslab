#include <kernel.h>
#include <stdint.h>
#include <syscall.h>
#include <unistd.h>

static const long IGNORE = 0L;

static long invoke_syscall(long sysno, long arg0, long arg1, long arg2, long arg3, long arg4) {
    /* TODO: [p2-task3] implement invoke_syscall via inline assembly */
    long ret;
    asm volatile (
        "mv a0, %1\n"
        "mv a1, %2\n"
        "mv a2, %3\n"
        "mv a3, %4\n"
        "mv a4, %5\n"
        "mv a7, %6\n"
        "ecall\n"
        "mv %0, a0\n"
        : "=r"(ret)
        : "r"(arg0), "r"(arg1), "r"(arg2), "r"(arg3), "r"(arg4), "r"(sysno)
        : "a0", "a1", "a2", "a3", "a4", "a7"
    );
    return ret;
}

void sys_yield(void) {
    /* TODO: [p2-task1] call call_jmptab to implement sys_yield */
    /* TODO: [p2-task3] call invoke_syscall to implement sys_yield */
    invoke_syscall(SYSCALL_YIELD, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_move_cursor(int x, int y) {
    /* TODO: [p2-task1] call call_jmptab to implement sys_move_cursor */
    /* TODO: [p2-task3] call invoke_syscall to implement sys_move_cursor */
    invoke_syscall(SYSCALL_CURSOR, (long)x, (long)y, IGNORE, IGNORE, IGNORE);
}

void sys_move_cursor_row(int row) {
    invoke_syscall(SYSCALL_CURSOR_ROW, (long)row, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_move_cursor_col(int col) {
    invoke_syscall(SYSCALL_CURSOR_COL, (long)col, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_write(char* buff) {
    /* TODO: [p2-task1] call call_jmptab to implement sys_write */
    /* TODO: [p2-task3] call invoke_syscall to implement sys_write */
    invoke_syscall(SYSCALL_WRITE, (long)buff, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_reflush(void) {
    /* TODO: [p2-task1] call call_jmptab to implement sys_reflush */
    /* TODO: [p2-task3] call invoke_syscall to implement sys_reflush */
    invoke_syscall(SYSCALL_REFLUSH, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_clear(void) {
    invoke_syscall(SYSCALL_CLEAR, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

int sys_mutex_init(int key) {
    /* TODO: [p2-task2] call call_jmptab to implement sys_mutex_init */
    /* TODO: [p2-task3] call invoke_syscall to implement sys_mutex_init */
    return invoke_syscall(SYSCALL_LOCK_INIT, (long)key, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_mutex_acquire(int mutex_idx) {
    /* TODO: [p2-task2] call call_jmptab to implement sys_mutex_acquire */
    /* TODO: [p2-task3] call invoke_syscall to implement sys_mutex_acquire */
    invoke_syscall(SYSCALL_LOCK_ACQ, (long)mutex_idx, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_mutex_release(int mutex_idx) {
    /* TODO: [p2-task2] call call_jmptab to implement sys_mutex_release */
    /* TODO: [p2-task3] call invoke_syscall to implement sys_mutex_release */
    invoke_syscall(SYSCALL_LOCK_RELEASE, (long)mutex_idx, IGNORE, IGNORE, IGNORE, IGNORE);
}

long sys_get_timebase(void) {
    /* TODO: [p2-task3] call invoke_syscall to implement sys_get_timebase */
    return invoke_syscall(SYSCALL_GET_TIMEBASE, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

long sys_get_tick(void) {
    /* TODO: [p2-task3] call invoke_syscall to implement sys_get_tick */
    return invoke_syscall(SYSCALL_GET_TICK, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_sleep(uint32_t time) { /* TODO: [p2-task3] call invoke_syscall to implement sys_sleep */ 
    invoke_syscall(SYSCALL_SLEEP, time, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_set_sche_workload(int workload) {
    invoke_syscall(SYSCALL_SET_WORKLOAD, workload, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_task_show(void) {
    invoke_syscall(SYSCALL_TASK_SHOW, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

/************************************************************/
#ifdef S_CORE
pid_t  sys_exec(int id, int argc, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    /* TODO: [p3-task1] call invoke_syscall to implement sys_exec for S_CORE */
}    
#else
pid_t  sys_exec(char *name, int argc, char **argv)
{
    /* TODO: [p3-task1] call invoke_syscall to implement sys_exec */
    invoke_syscall(SYSCALL_EXEC, (long)name, (long)argc, (long)argv, IGNORE, IGNORE);
}
#endif

void sys_exit(void)
{
    /* TODO: [p3-task1] call invoke_syscall to implement sys_exit */
    invoke_syscall(SYSCALL_EXIT, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

int  sys_kill(pid_t pid)
{
    /* TODO: [p3-task1] call invoke_syscall to implement sys_kill */
    return invoke_syscall(SYSCALL_KILL, (long)pid, IGNORE, IGNORE, IGNORE, IGNORE);
}

int  sys_waitpid(pid_t pid)
{
    /* TODO: [p3-task1] call invoke_syscall to implement sys_waitpid */
    return invoke_syscall(SYSCALL_WAITPID, (long)pid, IGNORE, IGNORE, IGNORE, IGNORE);
}


void sys_ps(void)
{
    /* TODO: [p3-task1] call invoke_syscall to implement sys_ps */
    invoke_syscall(SYSCALL_PS, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

pid_t sys_getpid()
{
    /* TODO: [p3-task1] call invoke_syscall to implement sys_getpid */
    return invoke_syscall(SYSCALL_GETPID, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

int  sys_getchar(void)
{
    /* TODO: [p3-task1] call invoke_syscall to implement sys_getchar */
    return invoke_syscall(SYSCALL_READCH, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

int  sys_barrier_init(int key, int goal)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_barrier_init */
    return invoke_syscall(SYSCALL_BARR_INIT, (long)key, (long)goal, IGNORE, IGNORE, IGNORE);
}

void sys_barrier_wait(int bar_idx)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_barrier_wait */
    invoke_syscall(SYSCALL_BARR_WAIT, (long)bar_idx, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_barrier_destroy(int bar_idx)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_barrier_destroy */
    invoke_syscall(SYSCALL_BARR_DESTROY, (long)bar_idx, IGNORE, IGNORE, IGNORE, IGNORE);
}

int sys_condition_init(int key)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_condition_init */
    return invoke_syscall(SYSCALL_COND_INIT, (long)key, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_condition_wait(int cond_idx, int mutex_idx)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_condition_wait */
    invoke_syscall(SYSCALL_COND_WAIT, (long)cond_idx, (long)mutex_idx, IGNORE, IGNORE, IGNORE);
}

void sys_condition_signal(int cond_idx)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_condition_signal */
    invoke_syscall(SYSCALL_COND_SIGNAL, (long)cond_idx, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_condition_broadcast(int cond_idx)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_condition_broadcast */
    invoke_syscall(SYSCALL_COND_BROADCAST, (long)cond_idx, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_condition_destroy(int cond_idx)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_condition_destroy */
    invoke_syscall(SYSCALL_COND_DESTROY, (long)cond_idx, IGNORE, IGNORE, IGNORE, IGNORE);
}

int sys_semaphore_init(int key, int init)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_semaphore_init */
}

void sys_semaphore_up(int sema_idx)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_semaphore_up */
}

void sys_semaphore_down(int sema_idx)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_semaphore_down */
}

void sys_semaphore_destroy(int sema_idx)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_semaphore_destroy */
}

int sys_mbox_open(char * name)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_mbox_open */
}

void sys_mbox_close(int mbox_id)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_mbox_close */
}

int sys_mbox_send(int mbox_idx, void *msg, int msg_length)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_mbox_send */
}

int sys_mbox_recv(int mbox_idx, void *msg, int msg_length)
{
    /* TODO: [p3-task2] call invoke_syscall to implement sys_mbox_recv */
}
/************************************************************/
