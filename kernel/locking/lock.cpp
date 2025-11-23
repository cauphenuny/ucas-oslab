extern "C" {

// #include <atomic.h>
#include <assert.h>
#include <breakpoint.h>
#include <guard.hpp>
#include <logger.h>
#include <os/list.h>
#include <os/lock.h>
#include <os/sched.h>

mutex_lock_t mlocks[LOCK_NUM];
bool mlock_used[LOCK_NUM] = {false};

void init_locks(void) {
    /* TODO: [p2-task2] initialize mlocks */
    for (int i = 0; i < LOCK_NUM; i++) {
        spin_lock_init(&mlocks[i].lock);
        list_init(&mlocks[i].block_queue);
    }
}

void spin_lock_init(spin_lock_t* lock) {
    /* TODO: [p2-task2] initialize spin lock */
    lock->status = UNLOCKED;
}

int spin_lock_try_acquire(spin_lock_t* lock) {
    /* TODO: [p2-task2] try to acquire spin lock */
    /**
     * @brief Atomically compare and exchange a value.
     *
     * Atomically compares the value at *ptr with *expected. If equal, replaces *ptr with desired.
     * Otherwise, loads the current value of *ptr into *expected.
     *
     * @param ptr        Pointer to the value to operate on.
     * @param expected   Pointer to the expected value; updated if comparison fails.
     * @param desired    The value to store if comparison succeeds.
     * @param weak       If true, allows spurious failure (usually set to false for locks).
     * @param success_memorder  Memory order for successful exchange (e.g., __ATOMIC_ACQUIRE).
     * @param failure_memorder  Memory order for failed exchange (e.g., __ATOMIC_RELAXED).
     * @return           true if the exchange took place, false otherwise.
     *
     * @note This is a GCC/Clang builtin for atomic compare-and-swap with flexible memory ordering.
     */
    lock_status_t expected = UNLOCKED;
    return __atomic_compare_exchange_n(
        &lock->status, &expected, LOCKED, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
    /*
        1. **__ATOMIC_RELAXED**
           - 只保证原子性，不做任何同步或重排序约束。
           - 适合无同步需求的计数器等场景。

        2. **__ATOMIC_CONSUME**
           - 只保证依赖性排序（很少用，实际实现常等同于 acquire）。

        3. **__ATOMIC_ACQUIRE**
           - 保证该原子操作之后的读写不会被重排序到操作之前。
           - 常用于加锁（lock）操作。

        4. **__ATOMIC_RELEASE**
           - 保证该原子操作之前的读写不会被重排序到操作之后。
           - 常用于解锁（unlock）操作。

        5. **__ATOMIC_ACQ_REL**
           - 结合 acquire 和 release，常用于读-改-写操作（如 fetch_add）。

        6. **__ATOMIC_SEQ_CST**
           - 顺序一致性，最强保证。所有原子操作全局有序，所有线程观察到的顺序一致。
           - 最安全但性能最低。
     */
}

void spin_lock_acquire(spin_lock_t* lock) {
    /* TODO: [p2-task2] acquire spin lock */

    // WARN: check failure order
    lock_status_t expected = UNLOCKED;
    while (!__atomic_compare_exchange_n(
        &lock->status, &expected, LOCKED, false, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED));
}

void spin_lock_release(spin_lock_t* lock) {
    /* TODO: [p2-task2] release spin lock */
    __atomic_store_n(&lock->status, UNLOCKED, __ATOMIC_RELEASE);
}

int do_mutex_lock_init(int key) {
    /* TODO: [p2-task2] initialize mutex lock */
    int id = -1;
    for (int i = 0; id == -1 && i < LOCK_NUM; i++) {
        with_spin guard(mlocks[i].lock);
        if (mlock_used[i] && mlocks[i].key == key) {
            id = i;
            pretty_log(LOG_INFO, "find existing mutex lock %d for key %d", id, key);
        }
    }
    for (int i = 0; id == -1 && i < LOCK_NUM; i++) {
        with_spin guard(mlocks[i].lock);
        if (!mlock_used[i]) {
            mlock_used[i] = true;
            mlocks[i].key = key;
            id = i;
            pretty_log(LOG_INFO, "allocate mutex lock %d from key %d", id, key);
        }
    }
    assert(id >= 0);
    // breakpoint();
    return id;
}

void do_mutex_lock_acquire(int mlock_idx) {
    /* TODO: [p2-task2] acquire mutex lock */
    pretty_log(LOG_INFO, "pid %d trying to acquire mutex lock %d", current_running->pid, mlock_idx);
    if (mlock_idx < 0 || mlock_idx >= LOCK_NUM) {
        pretty_loge("mutex lock index %d out of range!", mlock_idx);
        return;
    }
    mutex_lock_t* mutex = &mlocks[mlock_idx];
    while (true) {
        {
            with_spin guard(mutex->lock);
            if (!mutex->acquired) {
                mutex->acquired = 1;
                mutex->pid = current_running->pid;
                pretty_log(
                    LOG_INFO, "mutex lock %d acquired by pid %d", mlock_idx, current_running->pid);
                break;
            } else {
                pretty_log(
                    LOG_INFO, "mutex lock %d is already acquired by pid %d, blocking pid %d",
                    mlock_idx, mutex->pid, current_running->pid);
                do_block(&current_running->list, &mutex->block_queue);
                // breakpoint_set(BRK_DEBUG);
            }
        }
        do_scheduler();
    }
}

static void mutex_wakeup(int mlock_idx) {
    /// NOTE: need to be called with mutex lock held
    mutex_lock_t* mutex = mlocks + mlock_idx;
    print_sched_queue(&mutex->block_queue, "mutex block_queue");
    for (list_node_t *node = mutex->block_queue.next, *next; node != &mutex->block_queue;
         node = next) {
        next = node->next;
        pcb_t* pcb = container_of(node, pcb_t, list);
        pretty_log(LOG_INFO, "waking up blocked pid %d on mutex lock %d", pcb->pid, mlock_idx);
        do_unblock(node);
    }
}

void do_mutex_lock_release(int mlock_idx) {
    /* TODO: [p2-task2] release mutex lock */
    if (mlock_idx < 0 || mlock_idx >= LOCK_NUM) {
        pretty_loge("mutex lock index %d out of range!", mlock_idx);
        return;
    }
    pretty_log(LOG_INFO, "pid %d releasing mutex lock %d", current_running->pid, mlock_idx);
    mutex_lock_t* mutex = &mlocks[mlock_idx];
    with_spin guard(mutex->lock);
    if (!mlock_used[mlock_idx]) {
        pretty_loge("mutex lock %d is not initialized!", mlock_idx);
    } else if (mutex->pid != current_running->pid) {
        pretty_loge(
            "mutex lock %d is acquired by pid %d, cannot be released by pid %d", mlock_idx,
            mutex->pid, current_running->pid);
    } else {
        mutex->acquired = 0;
        mutex_wakeup(mlock_idx);
    }
    return;
}

void cleanup_mutex(pid_t pid) {
    for (int i = 0; i < LOCK_NUM; i++) {
        with_spin guard(mlocks[i].lock);
        if (mlock_used[i] && mlocks[i].acquired && mlocks[i].pid == pid) {
            pretty_log(LOG_INFO, "cleaned up mutex lock %d for pid %d", i, pid);
            mlocks[i].acquired = 0;
            mutex_wakeup(i);
        }
    }
}
}
