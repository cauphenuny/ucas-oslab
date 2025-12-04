#ifndef __INCLUDE_LOADER_H__
#define __INCLUDE_LOADER_H__

#include <os/task.h>
#include <type.h>
#include <pgtable.h>

kva_t create_task_pgdir(const task_info_t* task);
uint64_t load_task_img(const task_info_t* task, uintptr_t pgdir);

#endif
