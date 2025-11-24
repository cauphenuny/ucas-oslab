#include "thread.h"

int thread_create(thread_t *thread, int (*start_routine)(int, char **), int argc, char **argv) {
    char* name = argc ? argv[0] : "<unknown>";
    pid_t pid = sys_exec_by_entry(name, (uint64_t)start_routine, argc, argv);
    if (pid) {
        *thread = pid;
        return 0;
    }
    return 1;
}

int thread_join(thread_t thread) {
    int pid = sys_waitpid(thread);
    return pid != 0;
}
