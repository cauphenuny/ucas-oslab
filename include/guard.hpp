#include "os/lock.h"

struct with_spin {
    spin_lock_t& lock;
    with_spin(spin_lock_t& lk) : lock(lk) { spin_lock_acquire(&lock); }
    ~with_spin() { spin_lock_release(&lock); }
};

struct without_spin {
    spin_lock_t& lock;
    without_spin(spin_lock_t& lk) : lock(lk) { spin_lock_release(&lock); }
    ~without_spin() { spin_lock_acquire(&lock); }
};

struct with_mutex {
    int mlock_idx;
    with_mutex(int idx) : mlock_idx(idx) { do_mutex_lock_acquire(mlock_idx); }
    ~with_mutex() { do_mutex_lock_release(mlock_idx); }
};

struct without_mutex {
    int mlock_idx;
    without_mutex(int idx) : mlock_idx(idx) { do_mutex_lock_release(mlock_idx); }
    ~without_mutex() { do_mutex_lock_acquire(mlock_idx); }
};
