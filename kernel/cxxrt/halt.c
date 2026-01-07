#include <logger.h>
#include <os/cxxrt.h>
#include <os/fs.h>

_Atomic int halted;

static void halt_spin() {
    pretty_logi("hart #%d halted.", get_current_cpu_id());
    unlock_kernel();
    while (1) asm volatile("" ::: "memory");
}

void do_halt() {
    shutdown_fs();
    cxxrt_teardown();
    printk("farewell.");
    halted = 1;
    wakeup_other_hart();
    halt_spin();
}

void check_halted() {
    if (halted) {
        halt_spin();
    }
}
