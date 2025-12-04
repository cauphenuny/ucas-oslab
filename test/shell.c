/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *            Copyright (C) 2018 Institute of Computing Technology, CAS
 *               Author : Han Shukai (email : hanshukai@ict.ac.cn)
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *                  The shell acts as a task running in user mode.
 *       The main function is to make system calls through the user's output.
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * */

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define COLOR_RED    31
#define COLOR_GREEN  32
#define COLOR_YELLOW 33
#define COLOR_BLUE   34
#define COLOR_BLACK  30
#define COLOR_RESET  0
#define COLOR_DIM    2

#define SHELL_BEGIN 10
#define SHELL_END   25

int shell_begin = SHELL_BEGIN;
int shell_end = SHELL_END;

#define BUFFER_LEN   80
#define COMMAND_LEN  16
#define ARGUMENT_LEN 16

#define log_info(fmt, ...)                                \
    do {                                                  \
        printf("%s: " fmt "\n", __func__, ##__VA_ARGS__); \
    } while (0)

const char* prompt = "> root@UCAS_OS: ";
int prompt_len;

typedef int (*handler_t)(int argc, char** argv);

typedef struct task {
    char* name;
    char* desc;
    void (*subcmd_linter)(int dest[], int argc, char** argv);
    handler_t handler;
} task_t;

extern const task_t COMMAND_TABLE[];
extern const int NUM_CMD;

typedef struct {
    int argc;
    char* argv[ARGUMENT_LEN];
} args_t;

args_t parse(char* raw, int maxn) {
    args_t result = {0};
    int isspace = 1;
    // printf("parse: raw = \"%s\", maxn = %d, addr = %x", raw, maxn, raw), endl();
    for (int i = 0; i < maxn; i++) {
        // printf("i = %d", i), endl();
        if (!raw[i]) break;
        if (raw[i] == ' ') {
            raw[i] = '\0';
            isspace = 1;
        } else {
            if (isspace) {
                // printf("argc = %d", result.argc), endl();
                result.argv[result.argc] = &raw[i];
                result.argc++;
                isspace = 0;
            }
        }
    }
    return result;
}

char* shift(int* argc, char*** argv) {
    if (*argc == 0) return NULL;
    char* ret = (*argv)[0];
    (*argc)--;
    (*argv)++;
    return ret;
}

int shifti(int* argc, char*** argv) {
    char* buffer = shift(argc, argv);
    return atoi(buffer);
}

task_t* lint(int dest[], int argc, char** argv) {
    memset((void*)dest, 0, sizeof(dest[0]) * argc);
    task_t* matched = NULL;
    if (!argv[0]) return matched;
    for (int i = 0; i < NUM_CMD; i++) {
        if (strcmp(argv[0], COMMAND_TABLE[i].name) == 0) {
            matched = (task_t*)&COMMAND_TABLE[i];
            break;
        }
    }
    if (!matched) {
        dest[0] = COLOR_RED;
        dest[1] = COLOR_RESET;
    } else {
        dest[0] = COLOR_GREEN;
        matched->subcmd_linter(dest + 1, argc - 1, argv + 1);
    }
    return matched;
}

void render(char* buffer, int argc, int colors[]) {
    sys_screen_clear_color();
    int arg_id = -1;
    for (int i = 0; buffer[i]; i++) {
        while (buffer[i] && isspace(buffer[i])) i++;
        if (!buffer[i]) break;
        arg_id++;
        int start = i;
        while (buffer[i] && !isspace(buffer[i])) i++;
        int end = i;
        if (colors[arg_id]) {
            sys_screen_set_color(start + prompt_len, end + prompt_len, colors[arg_id], 0);
        }
    }
    sys_reflush();
}

#define BACKSPACE  127
#define BACKSPACE2 8
#define CTRL_U     21
#define CTRL_W     23
#define CTRL_N     14
#define CTRL_P     16
#define NEWLINE    13
#define TAB        9

int getchar() {
    int ch;
    while ((ch = sys_getchar()) == -1);
    // if (ch == BACKSPACE) {
    //     printf("\b \b");
    // } else {
    //     printf("%c", ch);
    // }
    return ch;
}

typedef struct {
    task_t* task;
    args_t args;
} command_t;

int issymbol(char ch) {
    const char* symbols = "~!@#$%^&*()-_=+[{]}\\|;:'\",<.>/?";
    for (int i = 0; symbols[i]; i++) {
        if (ch == symbols[i]) {
            return 1;
        }
    }
    return 0;
}

const char* complete(const char* start, const char* end);

command_t readline() {
    int pos = 0;
    static int last_pos = 0;
    char buffer[BUFFER_LEN] = {0};
    static char last_buffer[BUFFER_LEN] = {0};
    int color_buffer[ARGUMENT_LEN] = {0};
    static char args_buffer[BUFFER_LEN];
    memset(args_buffer, 0, BUFFER_LEN);
    int ch;
    args_t args = {0};
    task_t* task = NULL;

    while ((ch = getchar()) != NEWLINE) {
        switch (ch) {
            case BACKSPACE:
            case BACKSPACE2: {
                if (pos) {
                    pos--;
                    buffer[pos] = '\0';
                    printf("\b \b");
                }
                break;
            }

            case CTRL_U: {
                while (pos) {
                    pos--;
                    buffer[pos] = '\0';
                    printf("\b \b");
                }
                break;
            }

            case CTRL_W: {
                if (pos) {
                    pos--;
                    buffer[pos] = '\0';
                    printf("\b \b");
                    while (pos && !isspace(buffer[pos - 1])) {
                        pos--;
                        buffer[pos] = '\0';
                        printf("\b \b");
                    }
                }
                break;
            }

            case CTRL_N:
            case CTRL_P: {
                for (int i = 0; i < pos; i++) {
                    printf("\b \b");
                }
                char tmp[BUFFER_LEN];
                memcpy((uint8_t*)tmp, (uint8_t*)buffer, BUFFER_LEN);
                memcpy((uint8_t*)buffer, (uint8_t*)last_buffer, BUFFER_LEN);
                memcpy((uint8_t*)last_buffer, (uint8_t*)tmp, BUFFER_LEN);
                int tp = pos;
                pos = last_pos;
                last_pos = tp;
                for (int i = 0; i < pos; i++) {
                    printf("%c", buffer[i]);
                }
                break;
            }

            case TAB: {
                const char* completion = complete(buffer, buffer + pos);
                if (completion) {
                    while (*completion) {
                        printf("%c", *completion);
                        buffer[pos++] = *completion++;
                    }
                    printf(" ");
                    buffer[pos++] = ' ';
                }
                break;
            }

            default: {
                if (isalpha(ch) || isdigit(ch) || isspace(ch) || issymbol(ch)) {
                    buffer[pos++] = ch;
                    printf("%c", ch);
                }
                break;
            }
        }

        strcpy(args_buffer, buffer);
        args = parse(args_buffer, BUFFER_LEN);
        task = lint(color_buffer, args.argc, args.argv);
        render(buffer, args.argc, color_buffer);
        // display(display_buffer, display_color_buffer);
    }

    printf("\n");
    strcpy(last_buffer, buffer);
    last_pos = pos;
    return (command_t){task, args};
}

void preamble() {
    sys_move_cursor(0, shell_begin);
    printf("------------------- COMMAND -------------------\n");
}

int main(int argc, char** argv) {
    sys_screen_set_scroll(shell_begin + 1, shell_end);
    prompt_len = strlen(prompt);
    preamble();
    sys_set_sche_nice(20, sys_getpid());

    while (1) {
        // TODO [P3-task1]: call syscall to read UART port

        // TODO [P3-task1]: parse input
        // note: backspace maybe 8('\b') or 127(delete)

        // TODO [P3-task1]: ps, exec, kill, clear
        printf("> root@UCAS_OS: ");
        command_t cmd = readline();
        if (!cmd.task) {
            if (cmd.args.argc) {
                printf("%s: no such command: %s\n", argv[0], cmd.args.argv[0]);
            }
            continue;
        }
        int ret = cmd.task->handler(cmd.args.argc, cmd.args.argv);
        if (ret) {
            printf("%s: command `%s` exited with code %d\n", argv[0], cmd.args.argv[0], ret);
        }

        /************************************************************/
        /* Do not touch this comment. Reserved for future projects. */
        /************************************************************/
    }

    return 0;
}

void subcmd_lint(int dest[], int argc, char** argv) {
    static const char* const OP[] = {"&", "&&", "||", "|"};
    static const int NUM_OP = sizeof(OP) / sizeof(OP[0]);
    for (int i = 0; i < argc; i++) {
        for (int j = 0; j < NUM_OP; j++) {
            if (strcmp(argv[i], OP[j]) == 0) {
                dest[i] = COLOR_BLUE;
                break;
            }
        }
    }
}

void subcmd_lint_taskset(int dest[], int argc, char** argv) {
    subcmd_lint(dest, argc, argv);
    if (argc >= 1 && strcmp("-p", argv[0]) == 0) {
        dest[0] = COLOR_YELLOW;
    }
}

void subcmd_lint_help(int dest[], int argc, char** argv) {
    subcmd_lint(dest, argc, argv);
    if (argc >= 1 && strcmp("-a", argv[0]) == 0) {
        dest[0] = COLOR_YELLOW;
    }
}

int keycode(int argc, char** argv) {
    int ch = getchar();
    do {
        printf("%d\n", ch);
    } while ((ch = getchar()) != 27);
    return 0;
}

int echo(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        printf("%s ", argv[i]);
    }
    printf("\n");
    return 0;
}

int ps(int argc, char** argv) {
    sys_ps();
    return 0;
}

int ts(int argc, char** argv) {
    sys_task_show();
    return 0;
}

int exec(int argc, char** argv) {
    int wait;
    if (strcmp(argv[argc - 1], "&") == 0) {
        wait = 0;
        argc--;
    } else {
        wait = 1;
    }
    shift(&argc, &argv);
    if (argc <= 0) {
        log_info("usage: exec {name} [args ...] [&]");
        return 1;
    }
    pid_t pid = sys_exec(argv[0], argc, argv);
    if (!pid) {
        log_info("exec %s failed (argc=%d)", argv[0], argc);
        return 1;
    }
    if (wait) {
        sys_waitpid(pid);
    }
    return 0;
}

int kill(int argc, char** argv) {
    int pid = atoi(argv[1]);
    pid = sys_kill(pid);
    if (pid) return 0;
    return 1;
}

int clear(int argc, char** argv) {
    sys_clear();
    preamble();
    return 0;
}

int taskset(int argc, char** argv) {
    if (argc < 3) {
        log_info("usage: taskset {mask} {name} | taskset -p {mask} {pid}");
        return 1;
    }
    shift(&argc, &argv);
    if (strcmp(argv[0], "-p") == 0) {
        if (argc != 3) {
            log_info("usage: taskset -p {mask} {pid}");
            return 1;
        }
        shift(&argc, &argv);
        unsigned mask = shifti(&argc, &argv);
        int pid = shifti(&argc, &argv);
        int success = sys_set_affinity(pid, mask);
        if (!success) {
            log_info("set_affinity failed");
        } else {
            log_info("set pid %d with affinity 0x%x", pid, mask);
        }
        return !success;
    } else {
        unsigned mask = shifti(&argc, &argv);
        int wait;
        if (strcmp(argv[argc - 1], "&") == 0) {
            wait = 0;
            argc--;
        } else {
            wait = 1;
        }
        int pid = sys_exec_with_affinity(argv[0], argc, argv, mask);
        if (!pid) {
            log_info("exec failed");
            return 1;
        }
        if (wait) {
            sys_waitpid(pid);
        }
        return 0;
    }
}

int timebase;

void delay(int n) {
    if (!timebase) {
        timebase = sys_get_timebase();
    }
    for (volatile int i = 0; i < n * timebase / 5; i++);
}

int top(int argc, char** argv) {
    while (1) {
        int nproc = sys_ps();
        // sys_sleep(1);
        delay(1);
        int ch = sys_getchar();
        if (ch != -1) {
            break;
        }
        sys_screen_delete_line(nproc + 2);
    }
    return 0;
}

int info(int argc, char** argv) {
    if (argc < 1) return 1;
    return sys_display_info(argc, argv);
}

int set_height(int argc, char** argv) {
    if (argc != 3) {
        log_info("usage: set_height {start_row} {end_row}");
        return 1;
    }
    int start_row = atoi(argv[1]);
    int end_row = atoi(argv[2]);
    shell_begin = start_row;
    shell_end = end_row;
    sys_screen_clear_scroll();
    sys_screen_set_scroll(start_row + 1, end_row);
    sys_clear();
    preamble();
    return 0;
}

int help(int argc, char** argv) {
    printf("usage: command [subcmd ...]\n");
    const int CMD_LEN = 14;
    int display_all = (argc > 1 && strcmp("-a", argv[1]) == 0);
    for (int i = 0; i < NUM_CMD; i++) {
        if (!display_all && COMMAND_TABLE[i].name[0] == '.') continue;
        printf("  %s:", COMMAND_TABLE[i].name);
        sys_move_cursor_col(CMD_LEN);
        printf("%s\n", COMMAND_TABLE[i].desc);
    }
    return 0;
}

int exit(int argc, char** argv) {
    sys_exit();
    return 0;
}

int nice(int argc, char** argv) {
    if (argc != 2 && argc != 3) {
        log_info("usage: nice {nice_value} [pid]");
        return 1;
    }
    int nice_value = atoi(argv[1]);
    int pid = argc == 3 ? atoi(argv[2]) : sys_getpid();
    int err = sys_set_sche_nice(nice_value, pid);
    if (err) {
        log_info("set_nice failed");
    } else {
        log_info("set nice to %d for process %d", nice_value, pid);
    }
    return err;
}

int free(int argc, char** argv) {
    int human;
    if (argc == 1) {
        human = 0;
    } else {
        if (argc == 2 && strcmp("-h", argv[1]) == 0) {
            human = 1;
        } else {
            log_info("usage: free [-h]");
            return 1;
        }
    }
    size_t free_mem = sys_get_free_memory();
    if (human) {
        log_info("free memory: %d KB", free_mem / 1024);
    } else {
        log_info("free memory: %d bytes", free_mem);
    }
    return 0;
}

const task_t COMMAND_TABLE[] = {
    {"echo", "echo", subcmd_lint, echo},
    {"ts", "show task", subcmd_lint, ts},
    {"ps", "show process", subcmd_lint, ps},
    {"exec", "execute program", subcmd_lint, exec},
    {"kill", "kill process", subcmd_lint, kill},
    {"clear", "clear screen", subcmd_lint, clear},
    {"taskset", "set task affinity", subcmd_lint_taskset, taskset},
    {"top", "show top processes", subcmd_lint, top},
    {"info", "show system info", subcmd_lint, info},
    {"set_height", "set shell height", subcmd_lint, set_height},
    {"help", "show help information", subcmd_lint_help, help},
    {"exit", "exit shell", subcmd_lint, exit},
    {"nice", "set scheduling nice value", subcmd_lint, nice},
    {"free", "show free memory", subcmd_lint, free},
    {".keycode", "show keycode", subcmd_lint, keycode},
};

const int NUM_CMD = sizeof(COMMAND_TABLE) / sizeof(COMMAND_TABLE[0]);

const char* complete(const char* start, const char* end) {
    int len = end - start;
    const char* matched = NULL;
    for (int i = 0; i < NUM_CMD; i++) {
        if (strncmp(start, COMMAND_TABLE[i].name, len) == 0) {
            if (matched) {
                return NULL;
            }
            matched = COMMAND_TABLE[i].name + len;
        }
    }
    return matched;
}
