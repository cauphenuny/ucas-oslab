#include <atomic.h>
#include <logger.h>
#include <os/irq.h>
#include <os/kernel.h>
#include <os/lock.h>
#include <os/sched.h>
#include <os/smp.h>
#include <type.h>

static spin_lock_t kernel_lock;

void init_smp() {
    /* DONE: P3-TASK3 multicore*/
    spin_lock_init(&kernel_lock);
}

void wakeup_other_hart() {
    unsigned long mask = (1 << NR_CPUS) - 1;
    int hartid = get_current_cpu_id();
    mask = ~((~mask) | (1 << hartid));  // do not send ipi to self
    pretty_log(LOG_WARN, "send ipi with mask 0x%x", mask);
    send_ipi(&mask);
}

void lock_kernel(regs_context_t* regs, uint64_t stval, uint64_t scause, uint64_t sepc) {
    if (regs && (sepc & (1ul << 63)) && scause != (IRQC_S_TIMER | SCAUSE_IRQ_FLAG)) {
        // NOTE: exception occured in kernel code
        pretty_loge(
            "exception in kernel mode! sepc=0x%lx, scause=%lu, stval=0x%lx", sepc, scause, stval);
        handle_other(regs, stval, scause);
        return;
    }
    spin_lock_acquire(&kernel_lock);
    if (current_running->status == TASK_KILLED) {
        pretty_log(LOG_WARN, "current running process is killed, pid=%d", current_running->pid);
        do_exit();
    }
}

void unlock_kernel() {
    // pretty_log(LOG_INFO, "kernel unlocked.");
    spin_lock_release(&kernel_lock);
}
