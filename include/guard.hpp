#include <os/lock.h>

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
    mutex_lock_t& lock;
    with_mutex(mutex_lock_t& lk) : lock(lk) { mutex_acquire(&lock); }
    ~with_mutex() { mutex_release(&lock); }
};

struct without_mutex {
    mutex_lock_t& lock;
    without_mutex(mutex_lock_t& lk) : lock(lk) { mutex_release(&lock); }
    ~without_mutex() { mutex_acquire(&lock); }
};
