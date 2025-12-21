extern "C" {

#include <assert.h>
#include <breakpoint.h>
#include <guard.hpp>
#include <logger.h>
#include <os/list.h>
#include <os/lock.h>
#include <os/sched.h>
#include <os/string.h>

condition_t conditions[CONDITION_NUM];
pid_bitmap_t cond_ref[CONDITION_NUM] = {0};

void condition_init(condition_t* cond) {
    spin_lock_init(&cond->lock);
    list_init(&cond->wait_list, "cond");
}

void condition_destruct(condition_t* cond) { unblock_list(&cond->wait_list); }

void init_conditions() {
    for (int i = 0; i < CONDITION_NUM; i++) {
        cond_ref[i] = 0;
    }
}

void cleanup_conditions(pid_t pid) {
    for (int i = 0; i < CONDITION_NUM; i++) {
        with_spin guard(conditions[i].lock);
        int pcb_index = get_pcb_index(pid);
        if (cond_ref[i] & (1ull << pcb_index)) {
            cond_ref[i] &= ~(1ull << pcb_index);
            pretty_log(
                LOG_INFO, "released condition %d allocation for pid %d, remaining=0x%x", i, pid,
                cond_ref[i]);
            if (!cond_ref[i]) {
                pretty_log(LOG_INFO, "destroyed condition %d as no one is using it", i);
                condition_destruct(&conditions[i]);
            }
        }
    }
}

int do_condition_init(int key) {
    int id = -1;
    for (int i = 0; id == -1 && i < CONDITION_NUM; i++) {
        with_spin guard(conditions[i].lock);
        if (cond_ref[i] && conditions[i].key == key) {
            id = i;
            pretty_log(LOG_INFO, "find existing condition %d for key %d", id, key);
        }
    }
    for (int i = 0; id == -1 && i < CONDITION_NUM; i++) {
        with_spin guard(conditions[i].lock);
        if (!cond_ref[i]) {
            condition_init(&conditions[i]);
            conditions[i].key = key;
            id = i;
            pretty_log(LOG_INFO, "allocate condition %d for key %d", id, key);
        }
    }
    assert(id >= 0);
    int pcb_index = get_pcb_index(current_running->pid);
    cond_ref[id] |= (1ull << pcb_index);
    return id;
}

void condition_wait(condition_t* cond, mutex_lock_t* mutex) {
    {
        with_spin guard(cond->lock);
        pretty_log(LOG_INFO, "proc %d waiting condition 0x%x", current_running->pid, cond);
        do_block(&current_running->sched_node, &cond->wait_list);
    }
    without_mutex release_guard(*mutex);
    do_scheduler();
}

void condition_broadcast(condition_t* cond) {
    with_spin guard(cond->lock);
    // pretty_log(LOG_INFO, "broadcasting condition 0x%x", cond);
    unblock_list(&cond->wait_list);
}

void condition_signal(condition_t* cond) {
    with_spin guard(cond->lock);
    // pretty_log(LOG_INFO, "signaling condition 0x%x", cond);
    list_node_t* node = list_shift(&cond->wait_list);
    if (node) {
        pcb_t* pcb = container_of(node, pcb_t, sched_node);
        // pretty_log(LOG_INFO, "signaling pid %d on condition 0x%x", pcb->pid, cond);
        do_unblock(&pcb->sched_node);
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
    if (!cond_ref[cond_idx]) {
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

    if (!cond_ref[cond_idx]) {
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

    if (!cond_ref[cond_idx]) {
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
    if (!cond_ref[cond_idx]) {
        pretty_loge("condition %d is not initialized!", cond_idx);
        return;
    }

    pretty_log(LOG_INFO, "destroying condition %d", cond_idx);
    cond_ref[cond_idx] = 0;
    condition_destruct(&conditions[cond_idx]);
}

void show_conditions() {
    for (int i = 0; i < CONDITION_NUM; i++) {
        with_spin guard(conditions[i].lock);
        if (cond_ref[i]) {
            int waiting = list_size(&conditions[i].wait_list);
            printk(
                "condition %d: key=%d, ref=0x%x, waiting_count=%d\n", i, conditions[i].key,
                cond_ref[i], waiting);
        }
    }
}
}
