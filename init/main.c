#include "logger.h"

#include <asm.h>
#include <asm/unistd.h>
#include <assert.h>
#include <boot.h>
#include <breakpoint.h>
#include <common.h>
#include <csr.h>
#include <os/irq.h>
#include <os/kernel.h>
#include <os/loader.h>
#include <os/lock.h>
#include <os/mm.h>
#include <os/sched.h>
#include <os/smp.h>
#include <os/string.h>
#include <os/task.h>
#include <os/time.h>
#include <printk.h>
#include <screen.h>
#include <sys/syscall.h>
#include <type.h>

#define VERSION_BUF 50

int version = 3;  // version must between 0 and 9
char buf[VERSION_BUF];

static int bss_check(void) {
    for (int i = 0; i < VERSION_BUF; ++i) {
        if (buf[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static void init_jmptab(void) {
    volatile long (*(*jmptab))() = (volatile long (*(*))())KERNEL_JMPTAB_BASE;

    jmptab[CONSOLE_PUTSTR] = (volatile long (*)())port_write;
    jmptab[CONSOLE_PUTCHAR] = (volatile long (*)())port_write_ch;
    jmptab[CONSOLE_GETCHAR] = (volatile long (*)())port_read_ch;
    jmptab[SD_READ] = (volatile long (*)())sd_read;
    jmptab[SD_WRITE] = (volatile long (*)())sd_write;
    jmptab[QEMU_LOGGING] = (volatile long (*)())qemu_logging;
    jmptab[SET_TIMER] = (volatile long (*)())set_timer;
    jmptab[READ_FDT] = (volatile long (*)())read_fdt;
    jmptab[MOVE_CURSOR] = (volatile long (*)())screen_move_cursor;
    jmptab[PRINT] = (volatile long (*)())printk;
    jmptab[YIELD] = (volatile long (*)())do_scheduler;
    jmptab[MUTEX_INIT] = (volatile long (*)())do_mutex_lock_init;
    jmptab[MUTEX_ACQ] = (volatile long (*)())do_mutex_lock_acquire;
    jmptab[MUTEX_RELEASE] = (volatile long (*)())do_mutex_lock_release;
    jmptab[CONSOLE_REFLUSH] = (volatile long (*)())screen_reflush;
}

/************************************************************/

static void init_task_info(int argc, char** physical_argv) {
    // INFO:
    // argc: argc
    // argv+0: int task_num
    // argv+8: task_info_t* task_info
    // argv+16: int swap_location
    asserts(argc == 3, "invalid argc");
    uint64_t* argv = (void*)pa2kva((ptr_t)physical_argv);
    task_num = argv[0];
    pa_t physical_task_info = argv[1];
    memcpy((void*)tasks, (void*)pa2kva(physical_task_info), sizeof(task_info_t) * task_num);
    swap_base_location = argv[2];
    pretty_log(LOG_INFO, "[META] OS kernel arguments: ");
    pretty_log(LOG_INFO, "[META]   task_num: %d", task_num);
    pretty_log(LOG_INFO, "[META]   swap_location: %d", swap_base_location);
}

/*
 * Once a CPU core calls this function,
 * it will stop executing!
 */
void kernel_brake(void) {
    pretty_log(LOG_INFO, "brake hart #%d", get_current_cpu_id());
    disable_interrupt();
    while (1) __asm__ volatile("wfi");
}

_Atomic int initialized;      // hart 0 r/w, hart * r
_Atomic int booted[NR_CPUS];  // [i]: hart i r/w, hart * r

static bool all_booted() {
    for (int i = 0; i < NR_CPUS; i++) {
        if (!booted[i]) return false;
    }
    return true;
}

int main(int argc, char** argv) {

    int hartid = get_current_cpu_id();

    if (hartid == 0) {
        // Init jump table provided by kernel and bios(ΦωΦ)
        init_jmptab();
        init_logger();

        // Boot all hart (setup VM)
        pretty_log(LOG_INFO, "[INIT] hart #%d booted", hartid);
        booted[hartid] = 1;
        wakeup_other_hart();

        // Wait for all hart to boot, then reset boot mem mapping
        while (!all_booted());
        pretty_log(LOG_INFO, "[INIT] All harts booted");
        reset_boot_vm();
        pretty_log(LOG_INFO, "[INIT] Boot memory unmapped");

        // Check whether .bss section is set to zero
        int check = bss_check();
        asserts(check, ".bss check failed");

        // Init Process Control Blocks |•'-'•) ✧
        init_pcb();
        pretty_log(LOG_INFO, "[INIT] PCB initialization succeeded.");

        // Read CPU frequency (｡•ᴗ-)_
        init_timer();
        pretty_log(LOG_INFO, "[INIT] Timer initialized, time_base: %d", time_base);

        // Init lock mechanism o(´^｀)o
        init_locks();
        init_barriers();
        init_conditions();
        init_semaphores();
        init_mbox();
        init_smp();
        pretty_log(LOG_INFO, "[INIT] Sync mechanism initialization succeeded.");

        // Init interrupt (^_^)
        init_exception();
        pretty_log(LOG_INFO, "[INIT] Interrupt processing initialization succeeded.");

        // Init system call table (0_0)
        init_syscall();
        pretty_log(LOG_INFO, "[INIT] System call initialized successfully.");

        // Init screen (QAQ)
        init_screen();
        pretty_log(LOG_INFO, "[INIT] SCREEN initialization succeeded.");

        // Init virtual memory (>_<)
        init_vm();
        init_pagegroup();
        init_pipe();
        pretty_log(LOG_INFO, "[INIT] Memory initialization succeeded.");

        // Init task info (TAT)
        init_task_info(argc, argv);
        task_info_t* shell_task = find_task("shell");
        do_exec(shell_task, shell_task->entrance, 1, (char*[]){"shell"}, (unsigned)-1);
        pretty_log(LOG_INFO, "[INIT] Created shell process.");

        pretty_log(LOG_INFO, "[INIT] All done! Notifying other harts to continue...");
        initialized = 1;

    } else {
        pretty_log(LOG_INFO, "[INIT] hart #%d booted", hartid);
        booted[hartid] = 1;
        while (!initialized);

        setup_exception();
        current_running = &pcb_kernel[hartid];
        current_running->status = TASK_RUNNING;
        current_running->cpu = hartid;
    }

    pretty_log(LOG_INFO, "hart #%d launched", hartid);

    asm volatile("csrw sscratch, tp");

    enable_interrupt();
    reset_timer();

    while (true) {
        enable_preempt();
        asm volatile("wfi");
    }
    return 0;
}
