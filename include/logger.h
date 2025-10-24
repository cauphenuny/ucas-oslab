#ifndef _INCLUDE_LOG_H_
#define _INCLUDE_LOG_H_

#include <printk.h>

enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
};

const static char* log_level_str[] = {
    "DEBUG",
    "INFO ",
    "WARN ",
    "ERROR",
};

#define pretty_flog(level, fmt, ...)                                                        \
    do {                                                                                    \
        printl(                                                                             \
            "[%s] %s:%s:%d: " fmt "\n", log_level_str[level], __FILE__, __func__, __LINE__, \
            ##__VA_ARGS__);                                                                 \
    } while (0)

#define pretty_log(level, fmt, ...)                                                               \
    do {                                                                                          \
        printk("[%s] %s:%d: " fmt "\n", log_level_str[level], __FILE__, __LINE__, ##__VA_ARGS__); \
        printl("[%s] %s:%d: " fmt "\n", log_level_str[level], __FILE__, __LINE__, ##__VA_ARGS__); \
    } while (0)

#endif
