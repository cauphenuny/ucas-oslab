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

mutex_lock_t mlocks[LOCK_NUM];
bool mlock_used[LOCK_NUM] = {false};

void mutex_init(mutex_lock_t* mlock) {
    spin_lock_init(&mlock->lock);
    mlock->acquired = 0;
    mlock->pid = -1;
    mlock->key = -1;
    list_init(&mlock->block_list, "mutex");
}

void init_locks(void) {
    /* TODO: [p2-task2] initialize mlocks */
    spin_lock_init(&kernel_lock);
    for (int i = 0; i < LOCK_NUM; i++) {
        mlock_used[i] = false;
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
            mutex_init(&mlocks[i]);
            mlocks[i].key = key;
            mlock_used[i] = true;
            id = i;
            pretty_log(LOG_INFO, "allocate mutex lock %d from key %d", id, key);
        }
    }
    assert(id >= 0);
    // breakpoint();
    return id;
}

void mutex_acquire(mutex_lock_t* mutex) {
    while (true) {
        {
            with_spin guard(mutex->lock);
            if (!mutex->acquired) {
                mutex->acquired = 1;
                mutex->pid = current_running->pid;
                pretty_log(
                    LOG_INFO, "mutex lock 0x%x acquired by pid %d", mutex, current_running->pid);
                break;
            } else {
                pretty_log(
                    LOG_INFO, "mutex lock 0x%x is already acquired by pid %d, blocking pid %d",
                    mutex, mutex->pid, current_running->pid);
                do_block(&current_running->list, &mutex->block_list);
            }
        }
        do_scheduler();
    }
}

void mutex_release(mutex_lock_t* mutex) {
    mutex->acquired = 0;
    unblock_list(&mutex->block_list, "mutex block_list");
}

static void mutex_destruct(mutex_lock_t* mutex) { mutex_release(mutex); }

void do_mutex_lock_acquire(int mlock_idx) {
    /* TODO: [p2-task2] acquire mutex lock */
    pretty_log(LOG_INFO, "pid %d trying to acquire mutex lock %d", current_running->pid, mlock_idx);
    if (mlock_idx < 0 || mlock_idx >= LOCK_NUM) {
        pretty_loge("mutex lock index %d out of range!", mlock_idx);
        return;
    }
    mutex_acquire(&mlocks[mlock_idx]);
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
        mutex_release(mutex);
    }
    return;
}

void cleanup_mutex(pid_t pid) {
    for (int i = 0; i < LOCK_NUM; i++) {
        with_spin guard(mlocks[i].lock);
        if (mlock_used[i] && mlocks[i].acquired && mlocks[i].pid == pid) {
            pretty_log(LOG_INFO, "cleaned up mutex lock %d for pid %d", i, pid);
            mutex_destruct(&mlocks[i]);
        }
    }
}

barrier_t barriers[BARRIER_NUM];
int barrier_used[BARRIER_NUM] = {0};

void barrier_init(barrier_t* barrier) {
    barrier->goal = 0;
    barrier->current = 0;
    spin_lock_init(&barrier->lock);
    list_init(&barrier->block_list, "barrier");
}

void barrier_destruct(barrier_t* barrier) {
    unblock_list(&barrier->block_list, "barrier block_list (destroyed)");
}

void init_barriers(void) {
    for (int i = 0; i < BARRIER_NUM; i++) {
        barrier_used[i] = 0;
    }
}

int do_barrier_init(int key, int goal) {
    int id = -1;
    for (int i = 0; id == -1 && i < BARRIER_NUM; i++) {
        with_spin guard(barriers[i].lock);
        if (barrier_used[i] && barriers[i].key == key) {
            id = i;
            pretty_log(LOG_INFO, "find existing barrier %d for key %d", id, key);
            if (barriers[i].goal != goal) {
                pretty_loge(
                    "barrier %d goal mismatch: existing %d vs requested %d", id, barriers[i].goal,
                    goal);
            }
        }
    }
    for (int i = 0; id == -1 && i < BARRIER_NUM; i++) {
        with_spin guard(barriers[i].lock);
        if (!barrier_used[i]) {
            barrier_used[i] = 1;
            barrier_init(&barriers[i]);
            barriers[i].goal = goal;
            id = i;
            pretty_log(LOG_INFO, "allocate barrier %d for goal %d", id, goal);
        }
    }
    assert(id >= 0);
    return id;
}

void do_barrier_wait(int bar_idx) {
    if (bar_idx < 0 || bar_idx >= BARRIER_NUM) {
        pretty_loge("barrier index %d out of range!", bar_idx);
        return;
    }

    barrier_t* bar = &barriers[bar_idx];
    with_spin guard(bar->lock);

    if (!barrier_used[bar_idx]) {
        pretty_loge("barrier %d is not initialized!", bar_idx);
        return;
    }

    pretty_log(
        LOG_INFO, "proc %d waiting barrier %d(goal=%d, current=%d)", current_running->pid, bar_idx,
        bar->goal, bar->current);

    asserts(bar->current < bar->goal, "barrier broken");
    bar->current++;

    if (bar->current < bar->goal) {
        pretty_log(LOG_INFO, "proc %d blocked on barrier %d", current_running->pid, bar_idx);
        without_spin release_guard(bar->lock);
        do_block(&current_running->list, &bar->block_list);
        do_scheduler();
    } else {
        pretty_log(
            LOG_INFO, "proc %d caused barrier %d to be released", current_running->pid, bar_idx);
        bar->current = 0;
        unblock_list(&bar->block_list, "barrier block_list");
    }
}

void do_barrier_destroy(int bar_idx) {
    if (bar_idx < 0 || bar_idx >= BARRIER_NUM) {
        pretty_loge("barrier index %d out of range!", bar_idx);
        return;
    }

    barrier_t* bar = &barriers[bar_idx];
    with_spin guard(bar->lock);
    if (!barrier_used[bar_idx]) {
        pretty_loge("barrier %d is not initialized!", bar_idx);
        return;
    }

    barrier_used[bar_idx] = 0;
    barrier_destruct(bar);
    pretty_log(LOG_INFO, "destroyed barrier %d", bar_idx);
}

condition_t conditions[CONDITION_NUM];
int cond_used[CONDITION_NUM] = {0};

void condition_init(condition_t* cond) {
    spin_lock_init(&cond->lock);
    list_init(&cond->wait_list, "condition");
}

void condition_destruct(condition_t* cond) {
    unblock_list(&cond->wait_list, "condition wait_list (destroyed)");
}

void init_conditions() {
    for (int i = 0; i < CONDITION_NUM; i++) {
        cond_used[i] = 0;
    }
}

int do_condition_init(int key) {
    int id = -1;
    for (int i = 0; id == -1 && i < CONDITION_NUM; i++) {
        with_spin guard(conditions[i].lock);
        if (cond_used[i] && conditions[i].key == key) {
            id = i;
            pretty_log(LOG_INFO, "find existing condition %d for key %d", id, key);
        }
    }
    for (int i = 0; id == -1 && i < CONDITION_NUM; i++) {
        with_spin guard(conditions[i].lock);
        if (!cond_used[i]) {
            cond_used[i] = 1;
            condition_init(&conditions[i]);
            conditions[i].key = key;
            id = i;
            pretty_log(LOG_INFO, "allocate condition %d for key %d", id, key);
        }
    }
    assert(id >= 0);
    return id;
}

void condition_wait(condition_t* cond, mutex_lock_t* mutex) {
    {
        with_spin guard(cond->lock);
        pretty_log(LOG_INFO, "proc %d waiting condition 0x%x", current_running->pid, cond);
        do_block(&current_running->list, &cond->wait_list);
    }
    without_mutex release_guard(*mutex);
    do_scheduler();
}

void condition_broadcast(condition_t* cond) {
    with_spin guard(cond->lock);
    pretty_log(LOG_INFO, "broadcasting condition 0x%x", cond);
    unblock_list(&cond->wait_list, "condition wait_list");
}

void condition_signal(condition_t* cond) {
    with_spin guard(cond->lock);
    pretty_log(LOG_INFO, "signaling condition 0x%x", cond);
    list_node_t* node = list_shift(&cond->wait_list);
    if (node) {
        pcb_t* pcb = container_of(node, pcb_t, list);
        pretty_log(LOG_INFO, "signaling pid %d on condition 0x%x", pcb->pid, cond);
        do_unblock(&pcb->list);
    }
}

void do_condition_wait(int cond_idx, int mutex_idx) {
    if (cond_idx < 0 || cond_idx >= CONDITION_NUM) {
        pretty_loge("condition index %d out of range!", cond_idx);
        return;
    }
    if (mutex_idx < 0 || mutex_idx >= LOCK_NUM) {
        pretty_loge("mutex index %d out of range!", mutex_idx);
        return;
    }
    if (!cond_used[cond_idx]) {
        pretty_loge("condition %d is not initialized!", cond_idx);
        return;
    }
    condition_wait(&conditions[cond_idx], &mlocks[mutex_idx]);
}

void do_condition_broadcast(int cond_idx) {
    if (cond_idx < 0 || cond_idx >= CONDITION_NUM) {
        pretty_loge("condition index %d out of range!", cond_idx);
        return;
    }

    if (!cond_used[cond_idx]) {
        pretty_loge("condition %d is not initialized!", cond_idx);
        return;
    }
    condition_broadcast(&conditions[cond_idx]);
}

void do_condition_signal(int cond_idx) {
    if (cond_idx < 0 || cond_idx >= CONDITION_NUM) {
        pretty_loge("condition index %d out of range!", cond_idx);
        return;
    }

    if (!cond_used[cond_idx]) {
        pretty_loge("condition %d is not initialized!", cond_idx);
        return;
    }
    condition_signal(&conditions[cond_idx]);
}

void do_condition_destroy(int cond_idx) {
    if (cond_idx < 0 || cond_idx >= CONDITION_NUM) {
        pretty_loge("condition index %d out of range!", cond_idx);
        return;
    }

    with_spin guard(conditions[cond_idx].lock);
    if (!cond_used[cond_idx]) {
        pretty_loge("condition %d is not initialized!", cond_idx);
        return;
    }

    pretty_log(LOG_INFO, "destroying condition %d", cond_idx);
    cond_used[cond_idx] = 0;
    condition_destruct(&conditions[cond_idx]);
}

semaphore_t semaphores[SEMAPHORE_NUM];
int sema_used[SEMAPHORE_NUM] = {0};

void semaphore_init(semaphore_t* sema) {
    sema->key = -1;
    sema->count = 0;
    spin_lock_init(&sema->lock);
    list_init(&sema->wait_list, "semaphore");
}

void semaphore_destruct(semaphore_t* sema) {
    sema->key = -1;
    sema->count = 0;
    unblock_list(&sema->wait_list, "semaphore wait_list (destroyed)");
}

void init_semaphores() {
    for (int i = 0; i < SEMAPHORE_NUM; i++) {
        sema_used[i] = 0;
    }
}

int do_semaphore_init(int key, int init) {
    int id = -1;
    for (int i = 0; id == -1 && i < SEMAPHORE_NUM; i++) {
        with_spin guard(semaphores[i].lock);
        if (sema_used[i] && semaphores[i].key == key) {
            id = i;
            pretty_log(LOG_INFO, "find existing semaphore %d for key %d", id, key);
        }
    }
    for (int i = 0; id == -1 && i < SEMAPHORE_NUM; i++) {
        with_spin guard(semaphores[i].lock);
        if (!sema_used[i]) {
            sema_used[i] = 1;
            semaphore_init(&semaphores[i]);
            semaphores[i].key = key;
            semaphores[i].count = init;
            id = i;
            pretty_log(LOG_INFO, "allocate semaphore %d for key %d", id, key);
        }
    }
    assert(id >= 0);
    return id;
}

void do_semaphore_down(int sema_idx) {
    if (sema_idx < 0 || sema_idx >= SEMAPHORE_NUM) {
        pretty_loge("semaphore index %d out of range!", sema_idx);
        return;
    }

    semaphore_t* sema = &semaphores[sema_idx];
    with_spin guard(sema->lock);
    if (!sema_used[sema_idx]) {
        pretty_loge("semaphore %d is not initialized!", sema_idx);
        return;
    }

    sema->count--;
    pretty_log(LOG_INFO, "proc %d down semaphore %d", current_running->pid, sema_idx);
    if (sema->count < 0) {
        pretty_log(LOG_INFO, "proc %d block on semaphore %d", current_running->pid, sema_idx);
        do_block(&current_running->list, &sema->wait_list);
        without_spin release(sema->lock);
        do_scheduler();
    }
}

void do_semaphore_up(int sema_idx) {
    if (sema_idx < 0 || sema_idx >= SEMAPHORE_NUM) {
        pretty_loge("semaphore index %d out of range!", sema_idx);
        return;
    }

    semaphore_t* sema = &semaphores[sema_idx];
    with_spin guard(sema->lock);
    if (!sema_used[sema_idx]) {
        pretty_loge("semaphore %d is not initialized!", sema_idx);
        return;
    }

    sema->count++;
    pretty_log(LOG_INFO, "proc %d up semaphore %d", current_running->pid, sema_idx);
    if (sema->count <= 0) {
        list_node_t* node = list_shift(&sema->wait_list);
        asserts(node, "semaphore wait list is empty while count < 0");
        do_unblock(node);
    }
}

void do_semaphore_destroy(int sema_idx) {
    if (sema_idx < 0 || sema_idx >= SEMAPHORE_NUM) {
        pretty_loge("semaphore index %d out of range!", sema_idx);
    }
    semaphore_t* sema = &semaphores[sema_idx];
    with_spin guard(sema->lock);
    if (!sema_used[sema_idx]) {
        pretty_loge("semaphore %d is not initialized!", sema_idx);
        return;
    }

    pretty_log(LOG_INFO, "destroying semaphore %d", sema_idx);
    sema_used[sema_idx] = 0;
    semaphore_destruct(sema);
}

mailbox_t mailboxes[MBOX_NUM];
spin_lock_t mbox_locks[MBOX_NUM];
int mbox_allocated[MBOX_NUM] = {0};

void mailbox_init(mailbox_t* mbox) {
    memset(mbox->name, 0, sizeof(mbox->name));
    memset(mbox->buffer, 0, sizeof(mbox->buffer));
    mbox->head = 0;
    mbox->tail = 0;
    mbox->used = 0;
    mbox->nref = 0;
    mutex_init(&mbox->buffer_lock);
    condition_init(&mbox->empty);
    condition_init(&mbox->full);
}

void mailbox_destruct(mailbox_t* mbox) {
    condition_destruct(&mbox->full);
    condition_destruct(&mbox->empty);
    mutex_destruct(&mbox->buffer_lock);
}

void init_mbox() {
    for (int i = 0; i < MBOX_NUM; i++) {
        mbox_allocated[i] = 0;
        spin_lock_init(&mbox_locks[i]);
        mailbox_init(&mailboxes[i]);
    }
}

void list_mboxes() {
    for (int i = 0; i < MBOX_NUM; i++) {
        with_spin guard(mbox_locks[i]);
        if (mbox_allocated[i]) {
            pretty_log(
                LOG_INFO, "mbox %d: name=%s, nref=%d, used=%d", i, mailboxes[i].name,
                mailboxes[i].nref, mailboxes[i].used);
        }
    }
}

int do_mbox_open(char* name) {
    list_mboxes();
    int id = -1;
    for (int i = 0; id == -1 && i < MBOX_NUM; i++) {
        with_spin guard(mbox_locks[i]);
        if (mbox_allocated[i] && (strcmp(mailboxes[i].name, name) == 0)) {
            id = i;
            pretty_log(LOG_INFO, "find existing mailbox %d for name %s", id, name);
            mailboxes[id].nref++;
        }
    }
    for (int i = 0; id == -1 && i < MBOX_NUM; i++) {
        with_spin guard(mbox_locks[i]);
        if (!mbox_allocated[i]) {
            mbox_allocated[i] = 1;
            mailbox_init(&mailboxes[i]);
            strcpy(mailboxes[i].name, name);
            id = i;
            pretty_log(LOG_INFO, "allocate mailbox %d for name %s", id, name);
            mailboxes[id].nref++;
        }
    }
    assert(id >= 0);
    return id;
}

void do_mbox_close(int mbox_idx) {
    list_mboxes();
    if (mbox_idx < 0 || mbox_idx >= MBOX_NUM) {
        pretty_loge("mailbox index %d out of range!", mbox_idx);
        return;
    }

    mailbox_t* mbox = &mailboxes[mbox_idx];
    with_spin guard(mbox_locks[mbox_idx]);
    if (!mbox_allocated[mbox_idx]) {
        pretty_loge("mailbox %d is not initialized!", mbox_idx);
        return;
    }

    pretty_log(LOG_INFO, "closing mailbox %d", mbox_idx);
    mbox->nref--;
    if (mbox->nref > 0) {
        pretty_log(LOG_INFO, "mailbox %d still has %d references", mbox_idx, mbox->nref);
        return;
    }
    pretty_log(LOG_INFO, "destroying mailbox %d", mbox_idx);
    mbox_allocated[mbox_idx] = 0;
    mailbox_destruct(mbox);
}

int do_mbox_send(int mbox_idx, void* msg, int msg_length) {
    if (mbox_idx < 0 || mbox_idx >= MBOX_NUM) {
        pretty_loge("mailbox index %d out of range!", mbox_idx);
        return 0;
    }

    mailbox_t* mbox = &mailboxes[mbox_idx];
    if (!mbox_allocated[mbox_idx]) {
        pretty_loge("mailbox %d is not initialized!", mbox_idx);
        return 0;
    }

    int blocked = 0;
    while (true) {
        with_mutex guard(mbox->buffer_lock);
        int rest = MAX_MBOX_LENGTH - mbox->used;
        if (rest >= msg_length) {
            for (int i = 0; i < msg_length; i++) {
                mbox->buffer[mbox->tail] = ((char*)msg)[i];
                mbox->tail = (mbox->tail + 1) % MAX_MBOX_LENGTH;
                mbox->used++;
            }
            break;
        } else {
            blocked = 1;
            condition_wait(&mbox->full, &mbox->buffer_lock);
        }
    }
    condition_signal(&mbox->empty);
    return blocked;
}

int do_mbox_recv(int mbox_idx, void* msg, int msg_length) {
    if (mbox_idx < 0 || mbox_idx >= MBOX_NUM) {
        pretty_loge("mailbox index %d out of range!", mbox_idx);
        return 0;
    }

    mailbox_t* mbox = &mailboxes[mbox_idx];
    if (!mbox_allocated[mbox_idx]) {
        pretty_loge("mailbox %d is not initialized!", mbox_idx);
        return 0;
    }

    int blocked = 0;
    char* cur = (char*)msg;

    while (true) {
        with_mutex guard(mbox->buffer_lock);
        if (mbox->used >= msg_length) {
            for (int i = 0; i < msg_length; i++) {
                cur[i] = mbox->buffer[mbox->head];
                mbox->head = (mbox->head + 1) % MAX_MBOX_LENGTH;
                mbox->used--;
            }
            break;
        } else {
            blocked = 1;
            pretty_log(
                LOG_WARN, "blocking on mbox recv: used=%d, needed=%d", mbox->used, msg_length);
            condition_wait(&mbox->empty, &mbox->buffer_lock);
        }
    }
    condition_signal(&mbox->full);
    return blocked;
}
}
