/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *            Copyright (C) 2018 Institute of Computing Technology, CAS
 *               Author : Han Shukai (email : hanshukai@ict.ac.cn)
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *                                   Thread Lock
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

#ifndef INCLUDE_LOCK_H_
#define INCLUDE_LOCK_H_

#include <os/list.h>

#define LOCK_NUM 16

typedef enum {
    UNLOCKED,
    LOCKED,
} lock_status_t;

typedef struct spin_lock {
    volatile lock_status_t status;
} spin_lock_t;

extern spin_lock_t kernel_lock;

typedef struct mutex_lock {
    spin_lock_t lock;
    list_t block_list;  // container type: pcb_t
    int acquired, pid;
    int key;
} mutex_lock_t;

void spin_lock_init(spin_lock_t* lock);
int spin_lock_try_acquire(spin_lock_t* lock);
void spin_lock_acquire(spin_lock_t* lock);
void spin_lock_release(spin_lock_t* lock);

void mutex_acquire(mutex_lock_t* lock);
void mutex_release(mutex_lock_t* lock);

int do_mutex_lock_init(int key);
void do_mutex_lock_acquire(int mlock_idx);
void do_mutex_lock_release(int mlock_idx);

void show_mutexes();

void init_locks(void);

void cleanup_mutex(pid_t pid);

/************************************************************/
typedef struct barrier {
    int key;
    int goal;
    int current;
    spin_lock_t lock;
    list_t block_list;  // container type: pcb_t
} barrier_t;

#define BARRIER_NUM 16

void init_barriers(void);
int do_barrier_init(int key, int goal);
void do_barrier_wait(int bar_idx);
void do_barrier_destroy(int bar_idx);

void show_barriers();

typedef struct condition {
    int key;
    spin_lock_t lock;
    list_t wait_list;  // container type: pcb_t
} condition_t;

#define CONDITION_NUM 16

void init_conditions(void);
int do_condition_init(int key);
void do_condition_wait(int cond_idx, int mutex_idx);
void do_condition_signal(int cond_idx);
void do_condition_broadcast(int cond_idx);
void do_condition_destroy(int cond_idx);

void show_conditions();

typedef struct semaphore {
    int key;
    int count;
    spin_lock_t lock;
    list_t wait_list;  // container type: pcb_t
} semaphore_t;

#define SEMAPHORE_NUM 16

void init_semaphores(void);
int do_semaphore_init(int key, int init);
void do_semaphore_up(int sema_idx);
void do_semaphore_down(int sema_idx);
void do_semaphore_destroy(int sema_idx);

void show_semaphores();

#define MAX_MBOX_NAME   32
#define MAX_MBOX_LENGTH (64)

typedef struct mailbox {
    char name[MAX_MBOX_NAME];
    char buffer[MAX_MBOX_LENGTH];
    int nref, used;
    int head, tail;
    mutex_lock_t buffer_lock;
    condition_t empty, full;
} mailbox_t;

#define MBOX_NUM 16
void init_mbox();
int do_mbox_open(char* name);
void do_mbox_close(int mbox_idx);

/// @return 1: blocked, 0: immediately sent
int do_mbox_send(int mbox_idx, void* msg, int msg_length);

/// @return 1: blocked, 0: immediately received
int do_mbox_recv(int mbox_idx, void* msg, int msg_length);

void show_mailboxes();

/************************************************************/

#endif
