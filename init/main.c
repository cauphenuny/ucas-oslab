#include <asm.h>
#include <common.h>
#include <os/kernel.h>
#include <os/loader.h>
#include <os/string.h>
#include <os/task.h>
#include <type.h>

#define VERSION_BUF 50

int version = 3;  // version must between 0 and 9
char buf[VERSION_BUF];

// Task info array
task_info_t tasks[TASK_MAXNUM];

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
}

static void init_task_info(void) {
    // TODO: [p1-task4] Init 'tasks' array via reading app-info sector
    // NOTE: You need to get some related arguments from bootblock first
}

/************************************************************/
/* Do not touch this comment. Reserved for future projects. */
/************************************************************/

static int blocked_bios_getchar() {
    while (1) {
        int ch = bios_getchar();
        if (ch != -1) {
            return ch;
        }
    }
}

static int echoed_bios_getchar() {
    int ch = blocked_bios_getchar();
    bios_putchar(ch);
    if (ch == '\r') bios_putchar('\n');
    return ch;
}

static int isdigit(char c) { return c >= '0' && c <= '9'; }

static int readint() {
    char c = echoed_bios_getchar();
    while (!isdigit(c)) c = echoed_bios_getchar();
    int val = 0;
    while (isdigit(c)) {
        val = val * 10 + (c - '0');
        c = echoed_bios_getchar();
    }
    return val;
}

static void writeint(int val) {
    if (val == 0)
        bios_putchar('0');
    else {
        if (val / 10) writeint(val / 10);
        bios_putchar('0' + val % 10);
    }
}

int main(int argc, char** argv) {
    // INFO:
    // argc: task_num (in p1-task3)
    const int task_num = argc;

    // Check whether .bss section is set to zero
    int check = bss_check();

    // Init jump table provided by kernel and bios(ΦωΦ)
    init_jmptab();

    // Init task information (〃'▽'〃)
    init_task_info();

    // Output 'Hello OS!', bss check result and OS version
    char output_str[] = "bss check: _ version: _\n\r";
    char output_val[2] = {0};
    int i, output_val_pos = 0;

    output_val[0] = check ? 't' : 'f';
    output_val[1] = version + '0';
    for (i = 0; i < sizeof(output_str); ++i) {
        buf[i] = output_str[i];
        if (buf[i] == '_') {
            buf[i] = output_val[output_val_pos++];
        }
    }

    bios_putstr("Hello OS!\n\r");
    bios_putstr(buf);

    // while (true) {
    // int _ = echoed_bios_getchar();
    // bios_putchar(c);
    // bios_putchar('\n');
    // }

    // TODO: Load tasks by either task id [p1-task3] or task name [p1-task4],
    //   and then execute them.

    while (1) {
        bios_putstr("Input task id: ");
        int taskid = readint();
        if (taskid < 0 || taskid >= task_num) {
            bios_putstr("Invalid task id!\n\r");
            continue;
        } else {
            bios_putstr("Running task #");
            writeint(taskid);
            bios_putstr(":\n");
        }
        void (*task)() = (void (*)())(load_task_img(taskid));
        task();
        bios_putstr("Task completed.\n");
    }

    // Infinite while loop, where CPU stays in a low-power state (QAQQQQQQQQQQQ)
    while (1) {
        asm volatile("wfi");
    }

    return 0;
}
