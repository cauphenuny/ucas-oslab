#ifndef SMP_H
#define SMP_H

#include <type.h>

#define NR_CPUS 2
extern void init_smp();
extern void wakeup_other_hart();
extern uint64_t get_current_cpu_id();
struct regs_context;
extern void lock_kernel(struct regs_context* regs, uint64_t stval, uint64_t scause, uint64_t sepc);
extern void unlock_kernel();

#endif /* SMP_H */
