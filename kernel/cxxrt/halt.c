#include <logger.h>
#include <os/cxxrt.h>

_Atomic int halted;

static void halt_spin() {
    pretty_logi("hart #%d halted.", get_current_cpu_id());
    unlock_kernel();
    while (1) asm volatile("" ::: "memory");
}

void do_halt() {
    cxxrt_teardown();
    halted = 1;
    wakeup_other_hart();
    halt_spin();
}

void check_halted() {
    if (halted) {
        halt_spin();
    }
}

