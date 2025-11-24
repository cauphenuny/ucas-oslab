/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *            Copyright (C) 2018 Institute of Computing Technology, CAS
 *               Author : Han Shukai (email : hanshukai@ict.ac.cn)
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *                       System call related processing
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * */

#ifndef INCLUDE_SYSCALL_H_
#define INCLUDE_SYSCALL_H_

#include <os/sched.h>
#include <type.h>

#define NUM_SYSCALLS 96

/* syscall function pointer */
extern long (*syscall[NUM_SYSCALLS])();
extern void handle_syscall(regs_context_t* regs, uint64_t stval, uint64_t scause);

long sys_sleep(uint32_t);
long sys_yield(void);
long sys_exec(char*, int, char**);
long sys_exec_with_affinity(char*, int, char**, int);
long sys_exec_by_entry(char* name, int entrance, int argc, char* argv[]);
long sys_exit(void);
long sys_kill(pid_t);
long sys_waitpid(pid_t);
long sys_getpid(void);

long sys_process_show();
long sys_task_show();

long sys_write(char*);
long sys_readch();
long sys_move_cursor(int, int);
long sys_move_cursor_row(int);
long sys_move_cursor_col(int);
long sys_screen_reflush(void);
long sys_screen_clear(void);

long sys_get_timebase(void);
long sys_get_tick(void);

long sys_lock_init(int);
long sys_lock_acquire(int);
long sys_lock_release(int);

long sys_barrier_init(int key, int goal);
long sys_barrier_destroy(int bar_idx);
long sys_barrier_wait(int bar_idx);

long sys_condition_init(int key);
long sys_condition_wait(int cond_idx, int mutex_idx);
long sys_condition_signal(int cond_idx);
long sys_condition_broadcast(int cond_idx);
long sys_condition_destroy(int cond_idx);

long sys_semaphore_init(int key, int init);
long sys_semaphore_up(int sema_idx);
long sys_semaphore_down(int sema_idx);
long sys_semaphore_destroy(int sema_idx);

long sys_mbox_open(char* name);
long sys_mbox_recv(int mbox_idx, void* msg, int msg_length);
long sys_mbox_send(int mbox_idx, void* msg, int msg_length);
long sys_mbox_close(int mbox_id);

long sys_set_workload(int);
long sys_set_affinity(int pid, unsigned affinity_mask);

long sys_screen_set_scroll(int start_row, int end_row);
long sys_screen_clear_scroll(void);

long sys_screen_set_color(int start_col, int end_col, int foreground, int background);
long sys_screen_clear_color(void);

long sys_screen_delete_line(int nlines);

#endif
