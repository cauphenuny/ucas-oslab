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

    // TODO: [p2-task1] (S-core) initialize system call table.
    jmptab[CONSOLE_REFLUSH] = (volatile long (*)())screen_reflush;
}

static void init_task_info(void) {
    // TODO: [p1-task4] Init 'tasks' array via reading app-info sector
    // NOTE: You need to get some related arguments from bootblock first
}

/************************************************************/

extern pcb_t pcb[NUM_MAX_TASK];

static void init_pcb(void) {
    /* TODO: [p2-task1] load needed tasks and init their corresponding PCB */
    current_running = &pid0_pcb;
    current_running->status = TASK_RUNNING;

    for (int i = 0; i < task_num; i++) {
        load_task_img(tasks[i]);
    }

    for (int i = 0; i < NUM_MAX_TASK; i++) {
        pcb[i].status = TASK_EXITED;
    }
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

    syscall[SYSCALL_SET_WORKLOAD] = sys_set_workload;
    syscall[SYSCALL_TASK_SHOW] = sys_task_show;

}

/************************************************************/

static int getchar() {
    while (1) {
        int ch = bios_getchar();
        if (ch != -1) {
            return ch;
        }
    }
}

static int echoed_getchar() {
    int ch = getchar();
    bios_putchar(ch);
    if (ch == 127) bios_putstr("\b \b");
    return ch;
}

static int isdigit(char c) { return c >= '0' && c <= '9'; }

static int isalpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

static int readint() {
    char c = echoed_getchar();
    while (!isdigit(c)) c = echoed_getchar();
    int val = 0;
    while (isdigit(c)) {
        val = val * 10 + (c - '0');
        c = echoed_getchar();
    }
    return val;
}

static int readline(char* buffer, int size) {
    int count = 0;
    while (count < size - 1) {
        char c = echoed_getchar();
        if (c == '\n' || c == '\r') {
            break;
        }
        if (c == 127) {
            if (count > 0) {
                buffer[--count] = 0;
            }
            continue;
        }
        buffer[count++] = c;
    }
    buffer[count] = 0;
    return count;
}

static void run_task(char* name) {
    task_info_t* task_info = NULL;
    for (int i = 0; i < task_num; i++) {
        if (strcmp(tasks[i].name, name) == 0) {
            task_info = tasks + i;
            break;
        }
    }
    if (!task_info) {
        bios_putstr("Invalid name!\n");
    } else {
        void (*task)() = (void (*)())(load_task_img(*task_info));
        bios_putstr("Loaded task.\n");
        task();
        bios_putstr("Task completed.\n");
    }
}

void write_batchfile(char* cmd, int location) { bios_sd_write((unsigned int)cmd, 1, location); }
void read_batchfile(char* cmd, int location) { bios_sd_read((unsigned int)cmd, 1, location); }

int main(int argc, char** argv) {
    // Init jump table provided by kernel and bios(ΦωΦ)
    init_jmptab();

    // INFO:
    // argc: argc
    // argv+0: int task_num
    // argv+8: task_info_t* task_info
    // argv+16: int batchfile_location
    if (argc != 3) {
        bios_putstr("Invalid argc!\n");
        return -1;
    }
    uint64_t* args = (void*)argv;
    task_num = args[0];
    task_info_t* task_info = (task_info_t*)args[1];
    memcpy((void*)tasks, (void*)task_info, sizeof(task_info_t) * task_num);
    int batchfile_location = args[2];

    // Check whether .bss section is set to zero
    int check = bss_check();
    if (!check) {
        bios_putstr("> [ERROR] .bss check failed");
        while (1) asm volatile("wfi");
    }

    pretty_log(LOG_INFO, "[META] OS kernel arguments: ");
    pretty_log(LOG_INFO, "[META] task_num: %d", task_num);
    pretty_log(LOG_INFO, "[META] batchfile_location: %d", batchfile_location);

    // Init Process Control Blocks |•'-'•) ✧
    init_pcb();
    pretty_log(LOG_INFO, "[INIT] PCB initialization succeeded.");

    do_exec("shell", 1, (char*[]){"shell"});

    // while (true) {
    // int _ = echoed_bios_getchar();
    // bios_putchar(c);
    // bios_putchar('\n');
    // }
    // Read CPU frequency (｡•ᴗ-)_
    time_base = bios_read_fdt(TIMEBASE);
    pretty_log(LOG_INFO, "time_base: %d", time_base);

    // Init lock mechanism o(´^｀)o
    init_locks();
    pretty_log(LOG_INFO, "[INIT] Lock mechanism initialization succeeded.");

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

    reset_timer();
    asm volatile("csrw sscratch, tp");
    // do_scheduler();
    while (1) {
        // If you do non-preemptive scheduling, it's used to surrender control
        // do_scheduler();

        // If you do preemptive scheduling, they're used to enable CSR_SIE and wfi
        enable_preempt();
        asm volatile("wfi");
    }
    return 0;
}
