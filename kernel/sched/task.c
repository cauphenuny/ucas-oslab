#include <assert.h>
#include <csr.h>
#include <logger.h>
#include <os/mm.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/task.h>

// Task info array
int task_num;
task_info_t tasks[TASK_MAXNUM];

task_info_t* find_task(const char* name) {
    for (int i = 0; i < task_num; i++) {
        if (strcmp(tasks[i].name, name) == 0) {
            return &tasks[i];
        }
    }
    return NULL;
}

extern void ret_from_exception();

#define SP_ALIGNMENT 16

static void init_pcb_stack(
    pcb_t* pcb, ptr_t kernel_stack, ptr_t user_stack, ptr_t entry_point, int argc, char** argv) {
    /* TODO: [p2-task3] initialization of registers on kernel stack
     * HINT: sp, ra, sepc, sstatus
     * NOTE: To run the task in user mode, you should set corresponding bits
     *     of sstatus(SPP, SPIE, etc.).
     */
    regs_context_t* pt_regs = (regs_context_t*)(kernel_stack - sizeof(regs_context_t));

    /* TODO: [p2-task1] set sp to simulate just returning from switch_to
     * NOTE: you should prepare a stack, and push some values to
     * simulate a callee-saved context.
     */
    switchto_context_t* pt_switchto =
        (switchto_context_t*)((ptr_t)pt_regs - sizeof(switchto_context_t));

    // copy arguments to user_stack
    user_stack -= sizeof(char*) * argc;
    char** user_argv = (char**)user_stack;
    for (int i = 0; i < argc; i++) {
        int len = strlen(argv[i]) + 1;
        user_stack -= len;
        strcpy((char*)user_stack, argv[i]);
        user_argv[i] = (char*)user_stack;
    }
    user_stack -= user_stack % SP_ALIGNMENT;

    pt_regs->sepc = entry_point;
    pt_regs->sstatus = SR_SPIE;  // set SPIE field in sstatus
    pt_regs->regs[REG_SP] = user_stack;
    pt_regs->regs[REG_A0] = argc;
    pt_regs->regs[REG_A1] = (reg_t)user_argv;

    pcb->kernel_sp = (ptr_t)pt_switchto;
    pcb->user_sp = user_stack;
    pt_switchto->regs[SWITCHTO_REG_RA] = (reg_t)ret_from_exception;
    pt_switchto->regs[SWITCHTO_REG_SP] = pcb->kernel_sp;
}

#define KERNEL_STACK_PAGES 1
#define USER_STACK_PAGES   4

pcb_t* construct_pcb(const char* name, int argc, char* argv[]) {
    task_info_t* task = find_task(name);
    if (!task) return NULL;
    pcb_t* pcb = alloc_pcb();
    if (!pcb) return NULL;
    pcb->status = TASK_READY;
    int kernel_stack_top = allocKernelPage(KERNEL_STACK_PAGES) + KERNEL_STACK_PAGES * PAGE_SIZE;
    int user_stack_top = allocUserPage(USER_STACK_PAGES) + USER_STACK_PAGES * PAGE_SIZE;
    pretty_log(LOG_DEBUG, "%s: ksp: 0x%x, usp: 0x%x", name, kernel_stack_top, user_stack_top);
    assert(pcb);
    pcb->status = TASK_READY;
    strcpy(pcb->name, task->name);
    init_pcb_stack(pcb, kernel_stack_top, user_stack_top, task->entrance, argc, argv);
    return pcb;
}
