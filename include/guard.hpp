#include "os/lock.h"

struct spin_guard_t {
    spin_lock_t& lock;
    spin_guard_t(spin_lock_t& lk) : lock(lk) { spin_lock_acquire(&lock); }
    ~spin_guard_t() { spin_lock_release(&lock); }
};

struct mutex_guard_t {
    int mlock_idx;
    mutex_guard_t(int idx) : mlock_idx(idx) {
        do_mutex_lock_acquire(mlock_idx);
    }
    ~mutex_guard_t() {
        do_mutex_lock_release(mlock_idx);
    }
};
