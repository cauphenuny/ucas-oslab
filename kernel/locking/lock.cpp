#include "atomic.h"
extern "C" {

// #include <atomic.h>
#include <assert.h>
#include <breakpoint.h>
#include <guard.hpp>
#include <logger.h>
#include <os/list.h>
#include <os/lock.h>
#include <os/sched.h>
#include <os/string.h>

void init_locks(void) {
    /* DONE: [p2-task2] initialize mlocks */
    for (int i = 0; i < LOCK_NUM; i++) {
        mlock_ref[i] = 0;
    }
}

void spin_lock_init(spin_lock_t* lock) {
    /* DONE: [p2-task2] initialize spin lock */
    lock->status = UNLOCKED;
}

int spin_lock_try_acquire(spin_lock_t* lock) {
    /* DONE: [p2-task2] try to acquire spin lock */
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
    /* DONE: [p2-task2] acquire spin lock */

    lock_status_t expected;
    do {
        expected = UNLOCKED;  // ★ reset every loop
    } while (!__atomic_compare_exchange_n(
        &lock->status, &expected, LOCKED, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED));
}

void spin_lock_release(spin_lock_t* lock) {
    /* DONE: [p2-task2] release spin lock */
    __atomic_store_n(&lock->status, UNLOCKED, __ATOMIC_RELEASE);
}
}
