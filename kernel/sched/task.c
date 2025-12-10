#include <assert.h>
#include <csr.h>
#include <logger.h>
#include <os/loader.h>
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
    printk("Total: %d tasks\n", task_num);
}

extern void ret_from_exception();

#define SP_ALIGNMENT 16

void fetch_pcb_info(const pcb_t* pcb, ptr_t* kernel_ra, ptr_t* user_ra) {
    void* ksp = (void*)pcb->kernel_sp;
    switchto_context_t* swtch_context = ksp;
    regs_context_t* regs = ksp + sizeof(switchto_context_t);
    *kernel_ra = swtch_context->regs[SWITCHTO_REG_RA];
    *user_ra = regs->sepc;
}

void init_pcb_stack(
    pcb_t* pcb, kva_t kernel_stack, uva_t user_stack, uva_t entry_point, int argc, char** argv) {
    /* DONE: [p2-task3] initialization of registers on kernel stack
     * HINT: sp, ra, sepc, sstatus
     * NOTE: To run the task in user mode, you should set corresponding bits
     *     of sstatus(SPP, SPIE, etc.).
     */
    pretty_log(
        LOG_DEBUG, "building pcb task at %lx with sp=%lx, ra=%lx", kernel_stack, user_stack,
        entry_point);
    regs_context_t* pt_regs = (regs_context_t*)(kernel_stack - sizeof(regs_context_t));

    /* DONE: [p2-task1] set sp to simulate just returning from switch_to
     * NOTE: you should prepare a stack, and push some values to
     * simulate a callee-saved context.
     */
    switchto_context_t* pt_switchto =
        (switchto_context_t*)((ptr_t)pt_regs - sizeof(switchto_context_t));

    // copy arguments to user_stack
    asserts(user_stack != 0 || argc == 0, "pass arguments needs user_stack != 0");
    user_stack -= sizeof(char*) * argc;
    char** user_argv = (char**)user_stack;
    for (int i = 0; i < argc; i++) {
        pretty_logi("copy argument %s to pgdir %lx", argv[i], pcb->pgdir);
        int len = strlen(argv[i]) + 1;
        user_stack -= len;
        strcpy_kva2uva(user_stack, argv[i], pcb->pgdir);
        memcpy_kva2uva(
            (intptr_t)&user_argv[i], (intptr_t)&user_stack, sizeof(user_stack), pcb->pgdir);
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

pcb_t* construct_pcb(
    const task_info_t* task, uint64_t entrance, int argc, char* argv[], int kernel_mem,
    int user_mem) {
    pretty_log(LOG_DEBUG, "constructing pcb for task %s", task->name);
    pcb_t* pcb = alloc_pcb();
    if (!pcb) return NULL;
    asserts(pcb->status == TASK_EXITED, "PCB is not free");
    pretty_log(LOG_DEBUG, "alloced pcb at 0x%x, node: 0x%x", pcb, &pcb->sched_node);
    memset(pcb, 0, sizeof(pcb_t));

    pcb->pgdir = create_task_pgdir(task);
    share_pgtable(pcb->pgdir, PGDIR_VA);
    pretty_logi("shared pgtable for task %s", task->name);
    load_task_img(task, pcb->pgdir);
    pretty_logi("loaded task image for task %s", task->name);

    kva_t kernel_stack_bottom = alloc_pageframe(PAGE_GROUP_KERNEL, kernel_mem),
          kernel_stack_base = kernel_stack_bottom + kernel_mem * PAGE_SIZE;
    uva_t user_stack_base = USER_STACK_ADDR,
          user_stack_bottom = user_stack_base - user_mem * PAGE_SIZE;

    pcb->pid = process_id++;
    list_init(&pcb->wait_list, "proc");
    pcb->status = TASK_READY;
    strncpy(pcb->name, task->name, sizeof(pcb->name) - 1);
    char* cmd = pcb->cmd;
    for (int i = 0, sum = 0; i < argc; i++) {
        int len = strlen(argv[i]) + 1;
        if (sum + len > sizeof(pcb->cmd) - 1) {
            pretty_log(LOG_WARN, "command line too long, truncated");
            break;
        }
        strcpy(cmd, argv[i]);
        if (i == argc - 1) break;
        strcat(cmd, " ");
        cmd += len;
    }

    list_init(&pcb->child_list, "child_list");
    pcb->kernel_stack_base = kernel_stack_base;
    pcb->kernel_stack_bottom = kernel_stack_bottom;
    pcb->user_stack_base = user_stack_base;
    pcb->user_stack_bottom = user_stack_bottom;
    init_pcb_stack(pcb, kernel_stack_base, user_stack_base, entrance, argc, argv);
    pretty_log(
        LOG_DEBUG, "pid %d: %s: ksp=%x, usp=%x, entry=%x", pcb->pid, task->name, kernel_stack_base,
        user_stack_base, task->entrance);
    return pcb;
}

int set_proc_affinity(pcb_t* pcb, unsigned affinity_mask) {
    unsigned valid_mask = (1 << NR_CPUS) - 1;
    if (!(affinity_mask & valid_mask)) {
        pretty_log(LOG_WARN, "affinity mask 0x%x invalid, no valid cpu bits", affinity_mask);
        return 1;
    }
    pcb->affinity = affinity_mask & valid_mask;
    return 0;
}

void init_pcb(void) {
    int cnt = 0;

    for (int i = 0; i < NR_CPUS; i++) {
        pcb_kernel[i] = (pcb_t){
            .kernel_sp = INIT_KERNEL_STACK + PAGE_SIZE * (i + 1),
            .user_sp = 0,
            .kernel_stack_bottom = INIT_KERNEL_STACK + PAGE_SIZE * i,
            .kernel_stack_base = INIT_KERNEL_STACK + PAGE_SIZE * (i + 1),
            .user_stack_bottom = INIT_KERNEL_STACK + PAGE_SIZE * i,
            .user_stack_base = INIT_KERNEL_STACK + PAGE_SIZE * (i + 1),
            .pgdir = PGDIR_VA,
            .pid = i,
            .status = TASK_READY,
            .affinity = 1 << i,
        };
        strcpy(pcb_kernel[i].name, "init");
        strcpy(pcb_kernel[i].cmd, "init");
        list_init(&pcb_kernel[i].wait_list, "proc");
        list_init(&pcb_kernel[i].child_list, "child_list");
        pcb_all[cnt++] = &pcb_kernel[i];
    }

    for (int i = 0; i < NUM_MAX_TASK; i++) {
        pcb_user[i].status = TASK_EXITED;
        pcb_all[cnt++] = &pcb_user[i];
    }

    asserts(cnt == (sizeof(pcb_all) / sizeof(pcb_all[0])), "pcb_all size broken");
    asserts(get_current_cpu_id() == 0, "init_pcb called on sub-hart");
    current_running = &pcb_kernel[0];
    current_running->status = TASK_RUNNING;
    current_running->cpu = 0;
}
