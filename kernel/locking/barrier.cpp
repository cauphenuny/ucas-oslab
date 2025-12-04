extern "C" {

#include <assert.h>
#include <breakpoint.h>
#include <guard.hpp>
#include <logger.h>
#include <os/list.h>
#include <os/lock.h>
#include <os/sched.h>
#include <os/string.h>

barrier_t barriers[BARRIER_NUM];
pid_bitmap_t barrier_ref[BARRIER_NUM] = {0};

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
        barrier_ref[i] = 0;
    }
}

void cleanup_barriers(pid_t pid) {
    for (int i = 0; i < BARRIER_NUM; i++) {
        with_spin guard(barriers[i].lock);
        int pcb_index = get_pcb_index(pid);
        if (barrier_ref[i] & (1ull << pcb_index)) {
            barrier_ref[i] &= ~(1ull << pcb_index);
            pretty_log(
                LOG_INFO, "released barrier %d allocation for pid %d, remaining=0x%x", i, pid,
                barrier_ref[i]);
            if (!barrier_ref[i]) {
                pretty_log(LOG_INFO, "destroyed barrier %d as no one is using it", i);
                barrier_destruct(&barriers[i]);
            }
        }
    }
}

int do_barrier_init(int key, int goal) {
    int id = -1;
    int pcb_index = get_pcb_index(current_running->pid);
    for (int i = 0; id == -1 && i < BARRIER_NUM; i++) {
        with_spin guard(barriers[i].lock);
        if (barrier_ref[i] && barriers[i].key == key) {
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
        if (!barrier_ref[i]) {
            barrier_init(&barriers[i]);
            barriers[i].goal = goal;
            id = i;
            pretty_log(LOG_INFO, "allocate barrier %d for goal %d", id, goal);
        }
    }
    assert(id >= 0);
    barrier_ref[id] |= (1ull << pcb_index);
    return id;
}

void do_barrier_wait(int bar_idx) {
    if (bar_idx < 0 || bar_idx >= BARRIER_NUM) {
        pretty_loge("barrier index %d out of range!", bar_idx);
        return;
    }

    barrier_t* bar = &barriers[bar_idx];
    with_spin guard(bar->lock);

    if (!barrier_ref[bar_idx]) {
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
        do_block(&current_running->sched_node, &bar->block_list);
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
    if (!barrier_ref[bar_idx]) {
        pretty_loge("barrier %d is not initialized!", bar_idx);
        return;
    }

    barrier_ref[bar_idx] = 0;
    barrier_destruct(bar);
    pretty_log(LOG_INFO, "destroyed barrier %d", bar_idx);
}

void show_barriers() {
    for (int i = 0; i < BARRIER_NUM; i++) {
        with_spin guard(barriers[i].lock);
        if (barrier_ref[i]) {
            printk(
                "barrier %d: key=%d, ref=0x%x, goal=%d, current=%d\n", i, barriers[i].key,
                barrier_ref[i], barriers[i].goal, barriers[i].current);
        }
    }
}

}
