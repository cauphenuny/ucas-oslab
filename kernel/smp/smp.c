#include <logger.h>
#include <atomic.h>
#include <os/sched.h>
#include <os/smp.h>
#include <os/lock.h>
#include <os/kernel.h>

static spin_lock_t kernel_lock;

void init_smp()
{
    /* DONE: P3-TASK3 multicore*/
    spin_lock_init(&kernel_lock);
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
    // static int tot = 0;
    // int id = tot++;
    // pretty_log(LOG_INFO, "locking kernel... (#%d, cur=%d)", id, kernel_lock.status);
    spin_lock_acquire(&kernel_lock);
    // pretty_log(LOG_INFO, "locked (#%d)", id);
}

void unlock_kernel()
{
    // pretty_log(LOG_INFO, "kernel unlocked.");
    spin_lock_release(&kernel_lock);
}
