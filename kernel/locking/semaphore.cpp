extern "C" {

#include <assert.h>
#include <breakpoint.h>
#include <guard.hpp>
#include <logger.h>
#include <os/list.h>
#include <os/lock.h>
#include <os/sched.h>
#include <os/string.h>

semaphore_t semaphores[SEMAPHORE_NUM];
pid_bitmap_t sema_ref[SEMAPHORE_NUM] = {0};

void semaphore_init(semaphore_t* sema) {
    sema->key = -1;
    sema->count = 0;
    spin_lock_init(&sema->lock);
    list_init(&sema->wait_list, "sema");
}

void semaphore_destruct(semaphore_t* sema) {
    sema->key = -1;
    sema->count = 0;
    unblock_list(&sema->wait_list);
}

void init_semaphores() {
    for (int i = 0; i < SEMAPHORE_NUM; i++) {
        sema_ref[i] = 0;
    }
}

void cleanup_semaphores(pid_t pid) {
    for (int i = 0; i < SEMAPHORE_NUM; i++) {
        with_spin guard(semaphores[i].lock);
        int pcb_index = get_pcb_index(pid);
        if (sema_ref[i] & (1ull << pcb_index)) {
            sema_ref[i] &= ~(1ull << pcb_index);
            pretty_log(
                LOG_INFO, "released semaphore %d allocation for pid %d, remaining=0x%x", i, pid,
                sema_ref[i]);
            if (!sema_ref[i]) {
                pretty_log(LOG_INFO, "destroyed semaphore %d as no one is using it", i);
                semaphore_destruct(&semaphores[i]);
            }
        }
    }
}

int do_semaphore_init(int key, int init) {
    int id = -1;
    for (int i = 0; id == -1 && i < SEMAPHORE_NUM; i++) {
        with_spin guard(semaphores[i].lock);
        if (sema_ref[i] && semaphores[i].key == key) {
            id = i;
            pretty_log(LOG_INFO, "find existing semaphore %d for key %d", id, key);
        }
    }
    for (int i = 0; id == -1 && i < SEMAPHORE_NUM; i++) {
        with_spin guard(semaphores[i].lock);
        if (!sema_ref[i]) {
            sema_ref[i] = 1;
            semaphore_init(&semaphores[i]);
            semaphores[i].key = key;
            semaphores[i].count = init;
            id = i;
            pretty_log(LOG_INFO, "allocate semaphore %d for key %d", id, key);
        }
    }
    assert(id >= 0);
    int pcb_index = get_pcb_index(current_running->pid);
    sema_ref[id] |= (1ull << pcb_index);
    return id;
}

void do_semaphore_down(int sema_idx) {
    if (sema_idx < 0 || sema_idx >= SEMAPHORE_NUM) {
        pretty_loge("semaphore index %d out of range!", sema_idx);
        return;
    }

    semaphore_t* sema = &semaphores[sema_idx];
    with_spin guard(sema->lock);
    if (!sema_ref[sema_idx]) {
        pretty_loge("semaphore %d is not initialized!", sema_idx);
        return;
    }

    sema->count--;
    pretty_log(LOG_INFO, "proc %d down semaphore %d", current_running->pid, sema_idx);
    if (sema->count < 0) {
        pretty_log(LOG_INFO, "proc %d block on semaphore %d", current_running->pid, sema_idx);
        do_block(&current_running->sched_node, &sema->wait_list);
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
    if (!sema_ref[sema_idx]) {
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
    if (!sema_ref[sema_idx]) {
        pretty_loge("semaphore %d is not initialized!", sema_idx);
        return;
    }

    pretty_log(LOG_INFO, "destroying semaphore %d", sema_idx);
    sema_ref[sema_idx] = 0;
    semaphore_destruct(sema);
}

void show_semaphores() {
    for (int i = 0; i < SEMAPHORE_NUM; i++) {
        with_spin guard(semaphores[i].lock);
        if (sema_ref[i]) {
            int waiting = list_size(&semaphores[i].wait_list);
            printk(
                "semaphore %d: key=%d, ref=0x%x, remain=%d, waiting_count=%d\n", i,
                semaphores[i].key, sema_ref[i], semaphores[i].count, waiting);
        }
    }
}
}
