#ifndef __THREAD_H__
#define __THREAD_H__

#include "unistd.h"

typedef pid_t thread_t;

int thread_create(thread_t* thread, int (*start_routine)(int, char**), int argc, char** argv);
int thread_join(thread_t thread);

#endif
