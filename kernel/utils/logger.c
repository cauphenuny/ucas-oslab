#include <logger.h>
#include <os/lock.h>

spin_lock_t logger_lock;

const char* log_level_str[] = {
    "[DEBUG]", "[INFO] ", "[WARN] ", "[NOTE] ", "[ERROR]", "[FATAL]",
};

const char* log_level_str_color[] = {
    COLOR_BLUE "[DEBUG]" COLOR_RESET,   COLOR_GREEN "[INFO] " COLOR_RESET,
    COLOR_YELLOW "[WARN] " COLOR_RESET, COLOR_MAGENTA "[NOTE] " COLOR_RESET,
    COLOR_RED "[ERROR]" COLOR_RESET,    COLOR_RED "[FATAL]" COLOR_RESET,
};

void init_logger() { spin_lock_init(&logger_lock); }
