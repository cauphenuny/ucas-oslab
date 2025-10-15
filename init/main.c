#include <asm.h>
#include <common.h>
#include <os/kernel.h>
#include <os/loader.h>
#include <os/string.h>
#include <os/task.h>
#include <type.h>

#define VERSION_BUF 50

#define TASK_RESULT 0x5ffffff0

int version = 3;  // version must between 0 and 9
char buf[VERSION_BUF];

// Task info array
int task_num;
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
    jmptab[SD_WRITE] = (volatile long (*)())sd_write;
}

static void init_task_info(void) {
    // TODO: [p1-task4] Init 'tasks' array via reading app-info sector
    // NOTE: You need to get some related arguments from bootblock first
}

/************************************************************/
/* Do not touch this comment. Reserved for future projects. */
/************************************************************/

static void writeint(int val) {
    if (val == 0)
        bios_putchar('0');
    else {
        if (val / 10) writeint(val / 10);
        bios_putchar('0' + val % 10);
    }
}

static void writeptr(void* ptr) {
    bios_putstr("0x");
    uint64_t val = (uint64_t)ptr;
    int started = 0;
    for (int i = 64; i >= 0; i -= 4) {
        int digit = (val >> i) & 0xf;
        if (digit || started || i == 0) {
            started = 1;
            if (digit < 10)
                bios_putchar('0' + digit);
            else
                bios_putchar('a' + (digit - 10));
        }
    }
}

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
    // writeint(ch);
    if (ch == '\r') bios_putchar('\n');
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
    bios_putstr("OS kernel arguments: \n");
    bios_putstr("task_num: "), writeint(task_num), bios_putstr("\n");
    bios_putstr("task_info: "), writeptr(task_info), bios_putstr("\n");
    bios_putstr("batchfile_location: "), writeint(batchfile_location), bios_putstr("\n");

    // while (true) {
    // int _ = echoed_bios_getchar();
    // bios_putchar(c);
    // bios_putchar('\n');
    // }

    // TODO: Load tasks by either task id [p1-task3] or task name [p1-task4],
    //   and then execute them.

    char cmd[SECTOR_SIZE] = {0};

    while (1) {
        bios_putstr("(main) ");
        char name[16];
        bzero(name, sizeof(name));
        readline(name, sizeof(name));
        if (strncmp(name, "/list", 5) == 0) {
            for (int i = 0; i < task_num; i++) {
                bios_putstr("Task #"), writeint(i), bios_putstr(":\t");
                bios_putstr(tasks[i].name), bios_putstr("\n");
            }
        } else if (strncmp(name, "/batch", 6) != 0) {
            run_task(name);
        } else {
            char* subcmd = name + 7;
            if (strcmp(subcmd, "load") == 0) {
                bzero(cmd, sizeof(cmd));
                read_batchfile(cmd, batchfile_location);
                bios_putstr("Loaded batchfile: "), bios_putstr(cmd), bios_putstr("\n");
            } else if (strcmp(subcmd, "store") == 0) {
                bios_putstr("(store-batch) ");
                bzero(cmd, sizeof(cmd));
                readline(cmd, sizeof(cmd));
                write_batchfile(cmd, batchfile_location);
            } else if (strcmp(subcmd, "run") == 0) {
                for (int i = 0; i < sizeof(cmd); i++) {
                    if (!isdigit(cmd[i]) && !isalpha(cmd[i])) {
                        cmd[i] = 0;
                    }
                }
                for (int i = 0; i < sizeof(cmd); i++) {
                    if (cmd[i]) {
                        bios_putstr("\nRun: "), bios_putstr(cmd + i), bios_putstr("\n");
                        run_task(cmd + i);
                        bios_putstr("Result: ");
                        int result = *(int*)TASK_RESULT;
                        writeint(result);
                        bios_putstr("\n");
                        while (cmd[i]) i++;
                    }
                }
            } else {
                bios_putstr("Invalid command!\n");
            }
            // save_batchfile(cmd);
        }
    }

    // Infinite while loop, where CPU stays in a low-power state (QAQQQQQQQQQQQ)
    while (1) {
        asm volatile("wfi");
    }

    return 0;
}
