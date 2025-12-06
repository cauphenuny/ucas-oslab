extern "C" {

#include <assert.h>
#include <breakpoint.h>
#include <guard.hpp>
#include <logger.h>
#include <os/list.h>
#include <os/lock.h>
#include <os/sched.h>
#include <os/string.h>

mutex_lock_t mlocks[LOCK_NUM];
pid_bitmap_t mlock_ref[LOCK_NUM] = {0};

void mutex_init(mutex_lock_t* mlock) {
    spin_lock_init(&mlock->lock);
    mlock->acquired = 0;
    mlock->pid = -1;
    mlock->key = -1;
    list_init(&mlock->block_list, "mutex");
}

void mutex_release(mutex_lock_t* mutex) {
    mutex->acquired = 0;
    unblock_list(&mutex->block_list, "mutex block_list");
}

void mutex_destruct(mutex_lock_t* mutex) {
    mutex->acquired = 0;
    unblock_list(&mutex->block_list, "mutex block_list (destroyed)");
    if (mutex - mlocks >= 0 && mutex - mlocks < LOCK_NUM) {
        int idx = mutex - mlocks;
        mlock_ref[idx] = 0;
    }
}

void cleanup_mutexes(pid_t pid) {
    for (int i = 0; i < LOCK_NUM; i++) {
        with_spin guard(mlocks[i].lock);
        if (mlock_ref[i] && mlocks[i].acquired && mlocks[i].pid == pid) {
            pretty_log(LOG_INFO, "cleaned up mutex lock %d for pid %d", i, pid);
            mutex_release(&mlocks[i]);
        }
    }
    for (int i = 0; i < LOCK_NUM; i++) {
        with_spin guard(mlocks[i].lock);
        int pcb_index = get_pcb_index(pid);
        if (mlock_ref[i] & (1ull << pcb_index)) {
            mlock_ref[i] &= ~(1ull << pcb_index);
            pretty_log(
                LOG_INFO, "released mutex lock %d allocation for pid %d, remaining=0x%x", i, pid,
                mlock_ref[i]);
            if (!mlock_ref[i]) {
                pretty_log(LOG_INFO, "destroyed mutex lock %d as no one is using it", i);
                mutex_destruct(&mlocks[i]);
            }
        }
    }
}

int do_mutex_lock_init(int key) {
    /* DONE: [p2-task2] initialize mutex lock */
    int pcb_index = get_pcb_index(current_running->pid);
    int id = -1;
    for (int i = 0; id == -1 && i < LOCK_NUM; i++) {
        with_spin guard(mlocks[i].lock);
        if (mlock_ref[i] && mlocks[i].key == key) {
            id = i;
            pretty_log(LOG_INFO, "find existing mutex lock %d for key %d", id, key);
        }
    }
    for (int i = 0; id == -1 && i < LOCK_NUM; i++) {
        with_spin guard(mlocks[i].lock);
        if (!mlock_ref[i]) {
            mutex_init(&mlocks[i]);
            mlocks[i].key = key;
            id = i;
            pretty_log(LOG_INFO, "allocate mutex lock %d from key %d", id, key);
        }
    }
    assert(id >= 0);
    mlock_ref[id] |= (1ull << pcb_index);
    return id;
}

void mutex_acquire(mutex_lock_t* mutex) {
    while (true) {
        {
            with_spin guard(mutex->lock);
            if (!mutex->acquired) {
                mutex->acquired = 1;
                mutex->pid = current_running->pid;
                // pretty_log(
                //     LOG_INFO, "mutex lock 0x%x acquired by pid %d", mutex, current_running->pid);
                break;
            } else {
                pretty_log(
                    LOG_INFO, "mutex lock 0x%x is already acquired by pid %d, blocking pid %d",
                    mutex, mutex->pid, current_running->pid);
                do_block(&current_running->sched_node, &mutex->block_list);
            }
        }
        do_scheduler();
    }
}

void do_mutex_lock_acquire(int mlock_idx) {
    /* DONE: [p2-task2] acquire mutex lock */
    pretty_log(LOG_INFO, "pid %d trying to acquire mutex lock %d", current_running->pid, mlock_idx);
    if (mlock_idx < 0 || mlock_idx >= LOCK_NUM) {
        pretty_loge("mutex lock index %d out of range!", mlock_idx);
        return;
    }
    mutex_acquire(&mlocks[mlock_idx]);
}

void do_mutex_lock_release(int mlock_idx) {
    /* DONE: [p2-task2] release mutex lock */
    if (mlock_idx < 0 || mlock_idx >= LOCK_NUM) {
        pretty_loge("mutex lock index %d out of range!", mlock_idx);
        return;
    }
    pretty_log(LOG_INFO, "pid %d releasing mutex lock %d", current_running->pid, mlock_idx);
    mutex_lock_t* mutex = &mlocks[mlock_idx];
    with_spin guard(mutex->lock);
    if (!mlock_ref[mlock_idx]) {
        pretty_loge("mutex lock %d is not initialized!", mlock_idx);
    } else if (mutex->pid != current_running->pid) {
        pretty_loge(
            "mutex lock %d is acquired by pid %d, cannot be released by pid %d", mlock_idx,
            mutex->pid, current_running->pid);
    } else {
        mutex_release(mutex);
    }
    return;
}

void show_mutexes() {
    for (int i = 0; i < LOCK_NUM; i++) {
        with_spin guard(mlocks[i].lock);
        if (mlock_ref[i]) {
            printk(
                "mutex %d: key=%d, ref=0x%x, acquired=%d, pid=%d\n", i, mlocks[i].key, mlock_ref[i],
                mlocks[i].acquired, mlocks[i].pid);
        }
    }
}
}
