#include <kernel.h>
#include <stdint.h>
#include <syscall.h>
#include <unistd.h>

static const long IGNORE = 0L;

static long invoke_syscall(long sysno, long arg0, long arg1, long arg2, long arg3, long arg4) {
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
    invoke_syscall(SYSCALL_YIELD, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_move_cursor(int x, int y) {
    invoke_syscall(SYSCALL_CURSOR, (long)x, (long)y, IGNORE, IGNORE, IGNORE);
}

void sys_move_cursor_row(int row) {
    invoke_syscall(SYSCALL_CURSOR_ROW, (long)row, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_move_cursor_col(int col) {
    invoke_syscall(SYSCALL_CURSOR_COL, (long)col, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_write(char* buff) {
    invoke_syscall(SYSCALL_WRITE, (long)buff, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_reflush(void) {
    invoke_syscall(SYSCALL_REFLUSH, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_clear(void) {
    invoke_syscall(SYSCALL_CLEAR, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

int sys_mutex_init(int key) {
    return invoke_syscall(SYSCALL_LOCK_INIT, (long)key, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_mutex_acquire(int mutex_idx) {
    invoke_syscall(SYSCALL_LOCK_ACQ, (long)mutex_idx, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_mutex_release(int mutex_idx) {
    invoke_syscall(SYSCALL_LOCK_RELEASE, (long)mutex_idx, IGNORE, IGNORE, IGNORE, IGNORE);
}

long sys_get_timebase(void) {
    return invoke_syscall(SYSCALL_GET_TIMEBASE, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

long sys_get_tick(void) {
    return invoke_syscall(SYSCALL_GET_TICK, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_sleep(uint32_t time) {
    invoke_syscall(SYSCALL_SLEEP, time, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_set_sche_workload(int workload) {
    invoke_syscall(SYSCALL_SET_WORKLOAD, workload, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_task_show(void) {
    invoke_syscall(SYSCALL_TASK_SHOW, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

int sys_set_affinity(int pid, unsigned int affinity_mask) {
    return invoke_syscall(SYSCALL_SET_AFFINITY, (long)pid, (long)affinity_mask, IGNORE, IGNORE, IGNORE);
}

int sys_exec_with_affinity(char* name, int argc, char *argv[], int affinity_mask) {
    return invoke_syscall(SYSCALL_EXEC_WITH_AFF, (long)name, (long)argc, (long)argv, (long)affinity_mask, IGNORE);
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
    invoke_syscall(SYSCALL_EXEC, (long)name, (long)argc, (long)argv, IGNORE, IGNORE);
}
#endif

void sys_exit(void)
{
    invoke_syscall(SYSCALL_EXIT, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

int  sys_kill(pid_t pid)
{
    return invoke_syscall(SYSCALL_KILL, (long)pid, IGNORE, IGNORE, IGNORE, IGNORE);
}

int  sys_waitpid(pid_t pid)
{
    return invoke_syscall(SYSCALL_WAITPID, (long)pid, IGNORE, IGNORE, IGNORE, IGNORE);
}


void sys_ps(void)
{
    invoke_syscall(SYSCALL_PS, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

pid_t sys_getpid()
{
    return invoke_syscall(SYSCALL_GETPID, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

int  sys_getchar(void)
{
    return invoke_syscall(SYSCALL_READCH, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

int  sys_barrier_init(int key, int goal)
{
    return invoke_syscall(SYSCALL_BARR_INIT, (long)key, (long)goal, IGNORE, IGNORE, IGNORE);
}

void sys_barrier_wait(int bar_idx)
{
    invoke_syscall(SYSCALL_BARR_WAIT, (long)bar_idx, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_barrier_destroy(int bar_idx)
{
    invoke_syscall(SYSCALL_BARR_DESTROY, (long)bar_idx, IGNORE, IGNORE, IGNORE, IGNORE);
}

int sys_condition_init(int key)
{
    return invoke_syscall(SYSCALL_COND_INIT, (long)key, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_condition_wait(int cond_idx, int mutex_idx)
{
    invoke_syscall(SYSCALL_COND_WAIT, (long)cond_idx, (long)mutex_idx, IGNORE, IGNORE, IGNORE);
}

void sys_condition_signal(int cond_idx)
{
    invoke_syscall(SYSCALL_COND_SIGNAL, (long)cond_idx, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_condition_broadcast(int cond_idx)
{
    invoke_syscall(SYSCALL_COND_BROADCAST, (long)cond_idx, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_condition_destroy(int cond_idx)
{
    invoke_syscall(SYSCALL_COND_DESTROY, (long)cond_idx, IGNORE, IGNORE, IGNORE, IGNORE);
}

int sys_semaphore_init(int key, int init)
{
    return invoke_syscall(SYSCALL_SEMA_INIT, (long)key, (long)init, IGNORE, IGNORE, IGNORE);
}

void sys_semaphore_up(int sema_idx)
{
    invoke_syscall(SYSCALL_SEMA_UP, (long)sema_idx, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_semaphore_down(int sema_idx)
{
    invoke_syscall(SYSCALL_SEMA_DOWN, (long)sema_idx, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_semaphore_destroy(int sema_idx)
{
    invoke_syscall(SYSCALL_SEMA_DESTROY, (long)sema_idx, IGNORE, IGNORE, IGNORE, IGNORE);
}

int sys_mbox_open(char * name)
{
    return invoke_syscall(SYSCALL_MBOX_OPEN, (long)name, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_mbox_close(int mbox_id)
{
    invoke_syscall(SYSCALL_MBOX_CLOSE, (long)mbox_id, IGNORE, IGNORE, IGNORE, IGNORE);
}

int sys_mbox_send(int mbox_idx, void *msg, int msg_length)
{
    return invoke_syscall(SYSCALL_MBOX_SEND, (long)mbox_idx, (long)msg, (long)msg_length, IGNORE, IGNORE);
}

int sys_mbox_recv(int mbox_idx, void *msg, int msg_length)
{
    return invoke_syscall(SYSCALL_MBOX_RECV, (long)mbox_idx, (long)msg, (long)msg_length, IGNORE, IGNORE);
}
/************************************************************/
