#ifndef _INCLUDE_LOG_H_
#define _INCLUDE_LOG_H_

#include <breakpoint.h>
#include <printk.h>
#include <common.h>

#define COLOR_BLACK   "\033[0;30m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_DIM     "\033[2m"
#define COLOR_ITALIC  "\033[3m"
#define COLOR_UNDER   "\033[4m"
#define COLOR_REVERSE "\033[7m"

enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
    LOG_FATAL,
};

const static char* log_level_str[] = {
    "[DEBUG]", "[INFO ]", "[WARN] ", "[ERROR]", "[FATAL]",
};

const static char* log_level_str_color[] = {
    COLOR_BLUE "[DEBUG]" COLOR_RESET,   COLOR_GREEN "[INFO ]" COLOR_RESET,
    COLOR_YELLOW "[WARN] " COLOR_RESET, COLOR_RED "[ERROR]" COLOR_RESET,
    COLOR_RED "[FATAL]" COLOR_RESET,
};

#define pretty_log(level, fmt, ...)                                                              \
    do {                                                                                         \
        printl(                                                                                  \
            "%s " COLOR_BLACK "%s:%d (%s): \t" COLOR_RESET fmt "\n", log_level_str_color[level], \
            __FILE__, __LINE__, __func__, ##__VA_ARGS__);                                        \
    } while (0)

#define pretty_loge(fmt, ...)                      \
    do {                                           \
        pretty_log(LOG_ERROR, fmt, ##__VA_ARGS__); \
        breakpoint();                              \
    } while (0)

#define pretty_ilog(level, fmt, ...)                                                              \
    do {                                                                                          \
        pretty_log(level, fmt, ##__VA_ARGS__);                                                    \
        printk("%s %s:%d: \t" fmt "\n", log_level_str[level], __FILE__, __LINE__, ##__VA_ARGS__); \
    } while (0)

#endif
