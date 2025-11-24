#include <logger.h>
#include <atomic.h>
#include <os/sched.h>
#include <os/smp.h>
#include <os/lock.h>
#include <os/kernel.h>

void smp_init()
{
    /* TODO: P3-TASK3 multicore*/
}

void wakeup_other_hart()
{
    unsigned long mask = (1 << NR_CPUS) - 1;
    int hartid = get_current_cpu_id();
    mask = ~((~mask) | (1 << hartid)); // do not send ipi to self
    pretty_log(LOG_WARN, "send ipi with mask 0x%x", mask);
    send_ipi(&mask);
}

void lock_kernel()
{
    pretty_log(LOG_INFO, "locking kernel...");
    spin_lock_acquire(&kernel_lock);
}

void unlock_kernel()
{
    pretty_log(LOG_INFO, "kernel unlocked...");
    spin_lock_release(&kernel_lock);
}
