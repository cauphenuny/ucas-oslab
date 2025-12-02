#ifndef _INCLUDE_LOG_H_
#define _INCLUDE_LOG_H_

#include <os/lock.h>
#include <breakpoint.h>
#include <common.h>
#include <printk.h>
#include <os/smp.h>

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

extern const char* log_level_str[];
extern const char* log_level_str_color[];

extern spin_lock_t logger_lock;

#define pretty_log(level, fmt, ...)                                                                \
    do {                                                                                           \
        spin_lock_acquire(&logger_lock);                                                           \
        printl(                                                                                    \
            "%s " COLOR_BLACK "%d|%s:%d (%s) \t" COLOR_RESET fmt "\n", log_level_str_color[level], \
            get_current_cpu_id(), __FILE__, __LINE__, __func__, ##__VA_ARGS__);                    \
        spin_lock_release(&logger_lock);                                                           \
    } while (0)

#define pretty_loge(fmt, ...)                      \
    do {                                           \
        pretty_log(LOG_ERROR, fmt, ##__VA_ARGS__); \
        breakpoint();                              \
    } while (0)

#define pretty_logi(fmt, ...)                      \
    do {                                           \
        pretty_log(LOG_INFO, fmt, ##__VA_ARGS__); \
    } while (0)

#define pretty_logd(fmt, ...)                      \
    do {                                           \
        pretty_log(LOG_DEBUG, fmt, ##__VA_ARGS__); \
    } while (0)

#define pretty_logw(fmt, ...)                      \
    do {                                           \
        pretty_log(LOG_WARN, fmt, ##__VA_ARGS__); \
    } while (0)

extern void init_logger();

#endif
