#include "logger.h"

#include <asm.h>
#include <asm/unistd.h>
#include <assert.h>
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

static void spin() {
    // NOTE: this function should NOT use stack pointer (because user_sp is 0 for kernel pcb)
    // NOTE: this function is runned under u-mode
    while (true) {
        asm volatile("wfi");
    }
}

static void init_pcb(void) {
    /* TODO: [p2-task1] load needed tasks and init their corresponding PCB */
    for (int i = 0; i < task_num; i++) {
        load_task_img(tasks[i]);
    }

    for (int i = 0; i < NUM_MAX_TASK; i++) {
        pcb[i].status = TASK_EXITED;
    }

    add_virtual_task("init", (ptr_t)spin);

    for (int i = 0; i < NR_CPUS; i++) {
        kernel_pcb[i] = construct_pcb("init", 0, NULL, 1, 0);
        asserts(kernel_pcb[i], "failed to create kernel_pcb");
    }

    current_running = kernel_pcb[0];
    current_running->status = TASK_RUNNING;
}

static void init_syscall(void) {
    // TODO: [p2-task3] initialize system call table.
    syscall[SYSCALL_EXEC] = sys_exec;
    syscall[SYSCALL_EXIT] = sys_exit;
    syscall[SYSCALL_SLEEP] = sys_sleep;
    syscall[SYSCALL_KILL] = sys_kill;
    syscall[SYSCALL_WAITPID] = sys_waitpid;
    syscall[SYSCALL_PS] = sys_process_show;
    syscall[SYSCALL_GETPID] = sys_getpid;
    syscall[SYSCALL_YIELD] = sys_yield;

    syscall[SYSCALL_WRITE] = sys_write;
    syscall[SYSCALL_READCH] = sys_readch;
    syscall[SYSCALL_CURSOR] = sys_move_cursor;
    syscall[SYSCALL_CURSOR_COL] = sys_move_cursor_col;
    syscall[SYSCALL_CURSOR_ROW] = sys_move_cursor_row;
    syscall[SYSCALL_REFLUSH] = sys_screen_reflush;
    syscall[SYSCALL_CLEAR] = sys_screen_clear;

    syscall[SYSCALL_GET_TIMEBASE] = sys_get_timebase;
    syscall[SYSCALL_GET_TICK] = sys_get_tick;

    syscall[SYSCALL_LOCK_INIT] = sys_lock_init;
    syscall[SYSCALL_LOCK_ACQ] = sys_lock_acquire;
    syscall[SYSCALL_LOCK_RELEASE] = sys_lock_release;

    syscall[SYSCALL_BARR_INIT] = sys_barrier_init;
    syscall[SYSCALL_BARR_WAIT] = sys_barrier_wait;
    syscall[SYSCALL_BARR_DESTROY] = sys_barrier_destroy;

    syscall[SYSCALL_COND_INIT] = sys_condition_init;
    syscall[SYSCALL_COND_WAIT] = sys_condition_wait;
    syscall[SYSCALL_COND_SIGNAL] = sys_condition_signal;
    syscall[SYSCALL_COND_BROADCAST] = sys_condition_broadcast;
    syscall[SYSCALL_COND_DESTROY] = sys_condition_destroy;

    syscall[SYSCALL_SEMA_INIT] = sys_semaphore_init;
    syscall[SYSCALL_SEMA_UP] = sys_semaphore_up;
    syscall[SYSCALL_SEMA_DOWN] = sys_semaphore_down;
    syscall[SYSCALL_SEMA_DESTROY] = sys_semaphore_destroy;

    syscall[SYSCALL_MBOX_OPEN] = sys_mbox_open;
    syscall[SYSCALL_MBOX_CLOSE] = sys_mbox_close;
    syscall[SYSCALL_MBOX_SEND] = sys_mbox_send;
    syscall[SYSCALL_MBOX_RECV] = sys_mbox_recv;

    syscall[SYSCALL_SET_WORKLOAD] = sys_set_workload;
    syscall[SYSCALL_TASK_SHOW] = sys_task_show;
}

/************************************************************/

void write_batchfile(char* cmd, int location) { bios_sd_write((unsigned int)cmd, 1, location); }
void read_batchfile(char* cmd, int location) { bios_sd_read((unsigned int)cmd, 1, location); }

spin_lock_t kernel_lock;

int start;
static int batchfile_location;

static void init_task_info(int argc, char** argv) {
    // INFO:
    // argc: argc
    // argv+0: int task_num
    // argv+8: task_info_t* task_info
    // argv+16: int batchfile_location
    asserts(argc == 3, "invalid argc");
    uint64_t* args = (void*)argv;
    task_num = args[0];
    task_info_t* task_info = (task_info_t*)args[1];
    memcpy((void*)tasks, (void*)task_info, sizeof(task_info_t) * task_num);
    batchfile_location = args[2];
}

int main(int argc, char** argv) {

    int hartid = get_current_cpu_id();

    if (hartid == 0) {
        // Init jump table provided by kernel and bios(ΦωΦ)
        init_jmptab();

        // Check whether .bss section is set to zero
        int check = bss_check();
        asserts(check, ".bss check failed");

        pretty_log(LOG_INFO, "[META] OS kernel arguments: ");
        pretty_log(LOG_INFO, "[META] task_num: %d", task_num);
        pretty_log(LOG_INFO, "[META] batchfile_location: %d", batchfile_location);

        init_task_info(argc, argv);

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

        // TODO: [p2-task4] Setup timer interrupt and enable all interrupt globally
        // NOTE: The function of sstatus.sie is different from sie's

        do_exec("shell", 1, (char*[]){"shell"});
        pretty_log(LOG_INFO, "[INIT] Created shell process.");

        reset_timer();

        start = 1;
        pretty_log(LOG_INFO, "[INIT] All done! Waking up other harts");
        wakeup_other_hart();

    } else {
        while (!start);
        current_running = kernel_pcb[hartid];
        current_running->status = TASK_RUNNING;
        setup_exception();
        reset_timer();
    }

    lock_kernel();
    pretty_log(LOG_INFO, "hart %d started", hartid);

    asm volatile("csrw sscratch, tp");
    do_scheduler();
    return 0;
}
