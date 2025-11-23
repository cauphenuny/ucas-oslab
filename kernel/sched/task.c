#include <assert.h>
#include <csr.h>
#include <logger.h>
#include <os/mm.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/task.h>
#include <screen.h>

// Task info array
int task_num;
task_info_t tasks[TASK_MAXNUM];

task_info_t* find_task(const char* name) {
    for (int i = 0; i < task_num; i++) {
        if (strcmp(tasks[i].name, name) == 0) {
            return &tasks[i];
        }
    }
    pretty_log(LOG_WARN, "task %s not found!", name);
    return NULL;
}

void show_tasks(void) {
    const int NAME_LEN = 16;
    const int ENTRANCE_LEN = 12;
    printk("TASK_NAME"), screen_move_cursor_col(NAME_LEN);
    printk("ENTRANCE"), screen_move_cursor_col(NAME_LEN + ENTRANCE_LEN);
    printk("NAME"), screen_move_cursor_col(NAME_LEN + ENTRANCE_LEN + NAME_LEN);
    printk("ENTRANCE\n");
    int len = 0;
    for (int i = 0; i < task_num; i++) {
        task_info_t* task = &tasks[i];
        printk("%s", task->name);
        len += NAME_LEN;
        screen_move_cursor_col(len);
        printk("0x%x", task->entrance);
        len += ENTRANCE_LEN;
        screen_move_cursor_col(len);
        if ((i + 1) % 2 == 0) {
            printk("\n");
            len = 0;
        }
    }
    if (task_num % 2 != 0) {
        printk("\n");
    }
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
    pretty_log(LOG_DEBUG, "constructing pcb for task %s", name);
    task_info_t* task = find_task(name);
    if (!task) return NULL;
    pcb_t* pcb = alloc_pcb();
    if (!pcb) return NULL;
    asserts(pcb->status == TASK_EXITED, "PCB is not free");
    pretty_log(LOG_DEBUG, "alloced pcb at 0x%x, node: 0x%x", pcb, &pcb->list);

    int kernel_stack_top = allocKernelPage(KERNEL_STACK_PAGES) + KERNEL_STACK_PAGES * PAGE_SIZE;
    int user_stack_top = allocUserPage(USER_STACK_PAGES) + USER_STACK_PAGES * PAGE_SIZE;

    memset(pcb, 0, sizeof(pcb_t));
    pcb->pid = process_id++;
    pcb->wait_list = (list_head){&pcb->wait_list, &pcb->wait_list};
    pcb->status = TASK_READY;
    strcpy(pcb->name, task->name);
    init_pcb_stack(pcb, kernel_stack_top, user_stack_top, task->entrance, argc, argv);
    pretty_log(LOG_DEBUG, "process id: %d", pcb->pid);
    pretty_log(LOG_DEBUG, "%s: ksp: 0x%x, usp: 0x%x", name, kernel_stack_top, user_stack_top);
    return pcb;
}
