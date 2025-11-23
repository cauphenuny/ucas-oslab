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
#include <string.h>
#include <unistd.h>
#include <vt100.h>

#define SHELL_BEGIN 10

#define BUFFER_LEN   64
#define COMMAND_LEN  16
#define ARGUMENT_LEN 16

#define log_info(fmt, ...)                                \
    do {                                                  \
        printf("%s: " fmt "\n", __func__, ##__VA_ARGS__); \
    } while (0)

const char* prompt = "> root@UCAS_OS: ";
int prompt_len;

int ps(int, char**);
int exec(int, char**);
int kill(int, char**);
int clear(int, char**);
int echo(int, char**);
int decompose(int, char**);
int keycode(int, char**);
void subcmd_lint(char**, int, char**);

typedef int (*handler_t)(int argc, char** argv);

typedef struct command {
    char* name;
    void (*subcmd_linter)(char* dest[], int argc, char** argv);
    handler_t handler;
} command_t;

const command_t COMMAND_TABLE[] = {
    {"echo", subcmd_lint, echo},       {"ps", subcmd_lint, ps},
    {"exec", subcmd_lint, exec},       {"kill", subcmd_lint, kill},
    {"clear", subcmd_lint, clear},     {"decompose", subcmd_lint, decompose},
    {"keycode", subcmd_lint, keycode},
};

const int NUM_CMD = sizeof(COMMAND_TABLE) / sizeof(COMMAND_TABLE[0]);

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

command_t* lint(char* dest[], int argc, char** argv) {
    memset((void*)dest, 0, sizeof(dest[0]) * argc);
    command_t* matched = NULL;
    if (!argv[0]) return matched;
    for (int i = 0; i < NUM_CMD; i++) {
        if (strcmp(argv[0], COMMAND_TABLE[i].name) == 0) {
            matched = (command_t*)&COMMAND_TABLE[i];
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

void render(char* dest, char** dest_color, int maxn, char* buffer, int argc, char** colors) {
    memset(dest, 0, maxn);
    memset((void*)dest_color, 0, maxn);
    char* top = dest;
    char* current_color = NULL;
    int arg_id = -1, prev_space = 1;
    for (int i = 0; buffer[i]; i++) {
        if (!isspace(i) && prev_space) {
            arg_id++;
            if (colors[arg_id]) {
                current_color = colors[arg_id];
            }
        }
        prev_space = isspace(buffer[i]);
        *top++ = buffer[i];
        *dest_color++ = current_color;
    }
}

#define BACKSPACE 127
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
    command_t* cmd;
    args_t args;
} context_t;

context_t readline() {
    int pos = 0;
    char buffer[BUFFER_LEN] = {0};
    char* color_buffer[ARGUMENT_LEN] = {0};
    char display_buffer[BUFFER_LEN] = {0};
    char* display_color_buffer[BUFFER_LEN] = {0};
    char args_buffer[BUFFER_LEN] = {0};
    int ch;
    args_t args;
    command_t* cmd = NULL;
    while ((ch = getchar()) != NEWLINE) {
        if (ch == BACKSPACE) {
            if (pos) {
                pos--;
                buffer[pos] = '\0';
                printf("\b \b");
            }
        } else {
            buffer[pos++] = ch;
            printf("%c", ch);
        }
        strcpy(args_buffer, buffer);
        args = parse(args_buffer, BUFFER_LEN);
        cmd = lint(color_buffer, args.argc, args.argv);
        // render(
        //     display_buffer, display_color_buffer, sizeof(display_buffer), buffer, args.argc,
        //     color_buffer);
        // display(display_buffer, display_color_buffer);
    }
    printf("\n");
    return (context_t){cmd, args};
}

void preamble() {
    sys_move_cursor(0, SHELL_BEGIN);
    printf("------------------- COMMAND -------------------\n");
}

int main(int argc, char** argv) {
    prompt_len = strlen(prompt);
    preamble();

    while (1) {
        // TODO [P3-task1]: call syscall to read UART port

        // TODO [P3-task1]: parse input
        // note: backspace maybe 8('\b') or 127(delete)

        // TODO [P3-task1]: ps, exec, kill, clear
        printf("> root@UCAS_OS: ");
        context_t context = readline();
        if (!context.cmd) {
            printf("%s: no such command: %s\n", argv[0], context.args.argv[0]);
            continue;
        }
        context.cmd->handler(context.args.argc, context.args.argv);

        /************************************************************/
        /* Do not touch this comment. Reserved for future projects. */
        /************************************************************/
    }

    return 0;
}

void subcmd_lint(char** dest, int argc, char** argv) { dest[0] = COLOR_RESET; }

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

int exec(int argc, char** argv) { return sys_exec(argv[1], argc - 1, argv + 1); }

int kill(int argc, char** argv) {
    log_info("not implemented yet.");
    return decompose(argc, argv);
}

int clear(int argc, char** argv) {
    sys_clear();
    preamble();
    return 0;
}
