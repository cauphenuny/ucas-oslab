#include <assert.h>
#include <csr.h>
#include <logger.h>
#include <os/irq.h>
#include <os/kernel.h>
#include <os/mm.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/time.h>
#include <printk.h>
#include <screen.h>

handler_t irq_table[IRQC_COUNT];
handler_t exc_table[EXCC_COUNT];

typedef struct {
    uint64_t sys_irq, sys_exc, user, idle, last;
} cpu_time_t;

cpu_time_t cpu_times[NR_CPUS];

void show_cputime() {
    for (int i = 0; i < NR_CPUS; i++) {
        cpu_time_t* tim = &cpu_times[i];
        uint64_t total = tim->sys_irq + tim->sys_exc + tim->user + tim->idle;
        printk(
            "cpu%d: idle=%d%%, user=%d%%, sys_irq=%d%%, sys_exc=%d%%\n", i, tim->idle * 100 / total,
            tim->user * 100 / total, tim->sys_irq * 100 / total, tim->sys_exc * 100 / total);
    }
    for (int i = 0; i < NR_CPUS; i++) {
        memset(&cpu_times[i], 0, sizeof(cpu_time_t));
    }
}

static void stack_sanity_check() {
    int san = 1;
    if (current_running->kernel_sp > current_running->kernel_stack_base ||
        current_running->kernel_sp < current_running->kernel_stack_bottom) {
        pretty_loge(
            "kernel stack overflow/underflow detected! pid=%d, sp=0x%lx, high=0x%lx, low=0x%lx",
            current_running->pid, current_running->kernel_sp, current_running->kernel_stack_base,
            current_running->kernel_stack_bottom);
        san = 0;
    }
    if (current_running->user_sp > current_running->user_stack_base ||
        current_running->user_sp < current_running->user_stack_bottom) {
        pretty_loge(
            "user stack overflow/underflow detected! pid=%d, sp=0x%lx, high=0x%lx, low=0x%lx",
            current_running->pid, current_running->user_sp, current_running->user_stack_base,
            current_running->user_stack_bottom);
        san = 0;
    }
    asserts(san, "stack sanity check failed");
}

const char* irq_name[IRQC_COUNT] = {
    [IRQC_S_SOFT] = "Supervisor software interrupt",
    [IRQC_S_TIMER] = "Supervisor timer interrupt",
    [IRQC_S_EXT] = "Supervisor external interrupt",
};

const char* exc_name[EXCC_COUNT] = {
    [EXCC_INST_MISALIGNED] = "Instruction address misaligned",
    [EXCC_INST_ACCESS] = "Instruction access fault",
    [EXCC_ILLEGAL_INST] = "Illegal instruction",
    [EXCC_BREAKPOINT] = "Breakpoint",
    [EXCC_LOAD_MISALIGNED] = "Load address misaligned",
    [EXCC_LOAD_ACCESS] = "Load access fault",
    [EXCC_STORE_MISALIGNED] = "Store/AMO address misaligned",
    [EXCC_STORE_ACCESS] = "Store/AMO access fault",
    [EXCC_SYSCALL] = "Environment call from U-mode",
    [EXCC_SUPER_SYSCALL] = "Environment call from S-mode",
    [EXCC_INST_PAGE_FAULT] = "Instruction page fault",
    [EXCC_LOAD_PAGE_FAULT] = "Load page fault",
    [EXCC_STORE_PAGE_FAULT] = "Store/AMO page fault",
};

const char* exception_name(int is_irq, uint64_t code) {
    if (is_irq) {
        if (code < IRQC_COUNT && irq_name[code]) {
            return irq_name[code];
        } else {
            return "Unknown IRQ";
        }
    } else {
        if (code < EXCC_COUNT && exc_name[code]) {
            return exc_name[code];
        } else {
            return "Unknown Exception";
        }
    }
}

void interrupt_helper(regs_context_t* regs, uint64_t stval, uint64_t scause) {
    int is_irq = (scause & SCAUSE_IRQ_FLAG) != 0;
    uint64_t exception_code = scause & (~SCAUSE_IRQ_FLAG);

    stack_sanity_check();

    int hartid = get_current_cpu_id();
    cpu_time_t* tim = &cpu_times[hartid];
    uint64_t new_tick = get_ticks();
    if (tim->last) {
        if (current_running->pid >= NR_CPUS)
            tim->user += new_tick - tim->last;
        else
            tim->idle += new_tick - tim->last;
    }
    tim->last = new_tick;

    // pretty_log(LOG_DEBUG, "cur_pid: %d, is_irq: %d, exception_code: %lu", current_running->pid,
    // is_irq, exception_code);
    if (current_running->status == TASK_KILLED) {
        pretty_log(LOG_WARN, "current running process is killed, pid=%d", current_running->pid);
        do_exit();
    }
    if (!((is_irq && exception_code < IRQC_COUNT) || (~is_irq && exception_code < EXCC_COUNT))) {
        pretty_log(
            LOG_ERROR, "invalid interrupt: is_irq=%d, exception_code=%lu", is_irq, exception_code);
        handle_other(regs, stval, scause);
    }
    if (is_irq) {
        // pretty_log(LOG_INFO, "handling irq: %lu", exception_code);
        irq_table[exception_code](regs, stval, scause);
    } else {
        // pretty_log(LOG_INFO, "handling exception: %lu", exception_code);
        open_user_memory();
        exc_table[exception_code](regs, stval, scause);
        close_user_memory();
    }

    new_tick = get_ticks();
    if (is_irq) {
        tim->sys_irq += new_tick - tim->last;
    } else {
        tim->sys_exc += new_tick - tim->last;
    }
    tim->last = new_tick;
}

void handle_irq_timer(regs_context_t* regs, uint64_t stval, uint64_t scause) {
    // DONE: [p2-task4] clock interrupt handler.
    // Note: use bios_set_timer to reset the timer and remember to reschedule
    // uint64_t ticks = get_ticks();
    // pretty_log(LOG_INFO, "handling irq timer, ticks=%d, stval=%d, scause=%d", ticks, stval,
    // scause);
    update_page_access(get_ticks());
    reset_timer();
    do_scheduler();
}

void handle_page_fault(regs_context_t* regs, uint64_t stval, uint64_t scause) {
    pretty_logi("handling page fault, stval=%lx, scause=%lu, name=%s", stval, scause, exception_name(0, scause));

    // check if non-allocated or swapped out
    PTE* pte = find_pte(stval, current_running->pgdir, false);
    if (!pte || !get_attribute(*pte, _PAGE_SOFT)) {
        alloc_page_va(stval, current_running->pgdir);
    } else {
        kva_t new_page = alloc_pageframe(find_pagegroup(current_running->pgdir), 1);
        swapin(stval, current_running->pgdir, new_page);
    }
}

void init_exception() {
    /* DONE: [p2-task3] initialize exc_table */
    /* NOTE: handle_syscall, handle_other, etc.*/
    for (int i = 0; i < EXCC_COUNT; i++) {
        exc_table[i] = handle_other;
    }
    exc_table[EXCC_SYSCALL] = handle_syscall;
    exc_table[EXCC_LOAD_PAGE_FAULT] = exc_table[EXCC_STORE_PAGE_FAULT] = exc_table[EXCC_INST_PAGE_FAULT] = handle_page_fault;

    /* DONE: [p2-task4] initialize irq_table */
    /* NOTE: handle_int, handle_other, etc.*/
    for (int i = 0; i < IRQC_COUNT; i++) {
        irq_table[i] = handle_other;
    }
    irq_table[IRQC_M_TIMER] = irq_table[IRQC_U_TIMER] = irq_table[IRQC_S_TIMER] = handle_irq_timer;

    /* DONE: [p2-task3] set up the entrypoint of exceptions */
    setup_exception();
}

void handle_other(regs_context_t* regs, uint64_t stval, uint64_t scause) {
    char* reg_name[] = {"zero ", " ra  ", " sp  ", " gp  ", " tp  ", " t0  ", " t1  ", " t2  ",
                        "s0/fp", " s1  ", " a0  ", " a1  ", " a2  ", " a3  ", " a4  ", " a5  ",
                        " a6  ", " a7  ", " s2  ", " s3  ", " s4  ", " s5  ", " s6  ", " s7  ",
                        " s8  ", " s9  ", " s10 ", " s11 ", " t3  ", " t4  ", " t5  ", " t6  "};
    int is_irq = (scause & SCAUSE_IRQ_FLAG) != 0;
    uint64_t exception_code = scause & (~SCAUSE_IRQ_FLAG);
    pretty_loge(
        "pid: %d, scause: %lu, name: %s, stval: %lx", current_running->pid, scause,
        exception_name(is_irq, exception_code), stval);
    for (int i = 0; i < 32; i += 3) {
        for (int j = 0; j < 3 && i + j < 32; ++j) {
            printk("%s : %016lx ", reg_name[i + j], regs->regs[i + j]);
        }
        printk("\n\r");
    }
    printk(
        "sstatus: 0x%lx sbadaddr: 0x%lx scause: %lu\n\r", regs->sstatus, regs->sbadaddr,
        regs->scause);
    printk("sepc: 0x%lx\n\r", regs->sepc);
    printk("tval: 0x%lx cause: 0x%lx\n", stval, scause);
    assert(0);
}
