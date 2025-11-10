#include "os/sched.h"
#include <assert.h>
#include <csr.h>
#include <logger.h>
#include <sys/syscall.h>

long (*syscall[NUM_SYSCALLS])();

void handle_syscall(regs_context_t* regs, uint64_t stval, uint64_t scause) {
    /* TODO: [p2-task3] handle syscall exception */
    /**
     * HINT: call syscall function like syscall[fn](arg0, arg1, arg2),
     * and pay attention to the return value and sepc
     */
    int is_irq = (scause & SCAUSE_IRQ_FLAG) == 1ull;
    assert(!is_irq);
    uint64_t exception_code = scause & (~SCAUSE_IRQ_FLAG);
    if (exception_code == 8) {
        pretty_log(LOG_INFO, "handling ecall from U-mode");
    } else if (exception_code == 9) {
        pretty_log(LOG_INFO, "handling ecall from S-mode");
    } else {
        assert(false);
    }
    int sysno = regs->regs[REG_A7];
    int arg0 = regs->regs[REG_A0];
    int arg1 = regs->regs[REG_A1];
    int arg2 = regs->regs[REG_A2];
    int arg3 = regs->regs[REG_A3];
    int arg4 = regs->regs[REG_A4];
    int arg5 = regs->regs[REG_A5];
    pretty_log(
        LOG_INFO, "syscall no: %d, args: %d, %d, %d, %d, %d, %d", sysno, arg0, arg1, arg2, arg3, arg4,
        arg5);
    long ret = syscall[sysno](arg0, arg1, arg2, arg3, arg4, arg5);
    regs->regs[REG_A0] = ret;
    regs->regs[REG_SEPC] += 4;
}
