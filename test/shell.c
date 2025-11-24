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

#define COLOR_RED 31
#define COLOR_GREEN 32
#define COLOR_YELLOW 33
#define COLOR_BLUE 34
#define COLOR_RESET 0

#define SHELL_BEGIN 10
#define SHELL_END 30

#define BUFFER_LEN   64
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
#define CTRL_U     21
#define CTRL_W     23
#define CTRL_N     14
#define CTRL_P     16
#define NEWLINE   '\r'

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

command_t readline() {
    int pos = 0;
    static int last_pos = 0;
    char buffer[BUFFER_LEN] = {0};
    static char last_buffer[BUFFER_LEN] = {0};
    int color_buffer[ARGUMENT_LEN] = {0};
    static char args_buffer[BUFFER_LEN];
    int ch;
    args_t args;
    task_t* task = NULL;
    while ((ch = getchar()) != NEWLINE) {
        switch (ch) {
            case BACKSPACE: {
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
                memcpy(tmp, buffer, BUFFER_LEN);
                memcpy(buffer, last_buffer, BUFFER_LEN);
                memcpy(last_buffer, tmp, BUFFER_LEN);
                int tp = pos;
                pos = last_pos;
                last_pos = tp;
                for (int i = 0; i < pos; i++) {
                    printf("%c", buffer[i]);
                }
                break;
            }
            default: {
                buffer[pos++] = ch;
                printf("%c", ch);
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
    sys_move_cursor(0, SHELL_BEGIN);
    printf("------------------- COMMAND -------------------\n");
}

int main(int argc, char** argv) {
    sys_screen_set_scroll(SHELL_BEGIN + 1, SHELL_END);
    prompt_len = strlen(prompt);
    preamble();

    while (1) {
        // TODO [P3-task1]: call syscall to read UART port

        // TODO [P3-task1]: parse input
        // note: backspace maybe 8('\b') or 127(delete)

        // TODO [P3-task1]: ps, exec, kill, clear
        printf("> root@UCAS_OS: ");
        command_t cmd = readline();
        if (!cmd.task) {
            printf("%s: no such command: %s\n", argv[0], cmd.args.argv[0]);
            continue;
        }
        int ret = cmd.task->handler(cmd.args.argc, cmd.args.argv);
        if (ret) {
            printf("%s: command %s exited with code %d\n", argv[0], cmd.args.argv[0], ret);
        }

        /************************************************************/
        /* Do not touch this comment. Reserved for future projects. */
        /************************************************************/
    }

    return 0;
}

void subcmd_lint(int dest[], int argc, char** argv) {
    dest[0] = COLOR_RESET;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "&") == 0) {
            dest[i] = COLOR_BLUE;
        } else if (strcmp(argv[i], "&&") == 0) {
            dest[i] = COLOR_BLUE;
        } else if (strcmp(argv[i], "||") == 0) {
            dest[i] = COLOR_BLUE;
        } else if (strcmp(argv[i], "|") == 0) {
            dest[i] = COLOR_BLUE;
        } else if (argv[i][0] == '-') {
            dest[i] = COLOR_YELLOW;
        } else {
            dest[i] = COLOR_RESET;
        }
    }
}

int keycode(int argc, char** argv) {
    int ch = getchar();
    do {
        sys_move_cursor_col(0);
        printf("%d   ", ch);
    } while ((ch = getchar()) != 27);
    printf("\n");
    return 0;
}

int echo(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        printf("%s ", argv[i]);
    }
    printf("\n");
    return 0;
}

int decompose(int argc, char** argv) {
    for (int i = 0; i < argc; i++) {
        log_info("[%d]: %s", i, argv[i]);
    }
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
    pid_t pid = sys_exec(argv[0], argc, argv);
    if (!pid) {
        log_info("exec failed");
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
        return 0;
    }
    shift(&argc, &argv);
    if (strcmp(argv[0], "-p") == 0) {
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

int top(int argc, char** argv) {
    while (1) {
        int nproc = sys_ps();
        sys_sleep(1);
        int ch = sys_getchar();
        if (ch != -1) {
            break;
        }
        sys_screen_delete_line(nproc + 2);
        printf("\n");
    }
    return 0;
}

int info(int argc, char** argv) {
    if (argc < 1) return 1;
    shift(&argc, &argv);
    return sys_display_info(argc, argv);
}

const task_t COMMAND_TABLE[] = {
    {"echo", subcmd_lint, echo},
    {"ts", subcmd_lint, ts},
    {"ps", subcmd_lint, ps},
    {"exec", subcmd_lint, exec},
    {"kill", subcmd_lint, kill},
    {"clear", subcmd_lint, clear},
    {"decompose", subcmd_lint, decompose},
    {"keycode", subcmd_lint, keycode},
    {"taskset", subcmd_lint, taskset},
    {"top", subcmd_lint, top},
    {"info", subcmd_lint, info},
};

const int NUM_CMD = sizeof(COMMAND_TABLE) / sizeof(COMMAND_TABLE[0]);
