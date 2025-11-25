#include <assert.h>
#include <logger.h>
#include <os/kernel.h>
#include <os/list.h>
#include <os/sched.h>
#include <os/time.h>
#include <type.h>

uint64_t time_elapsed = 0;
uint64_t time_base = 0;

void init_timer()
{
    time_base = bios_read_fdt(TIMEBASE);
}

uint64_t get_ticks()
{
    __asm__ __volatile__(
        "rdtime %0"
        : "=r"(time_elapsed));
    return time_elapsed;
}

void reset_timer()
{
    uint64_t ticks = get_ticks();
    set_timer(ticks + TIMER_INTERVAL);
}

uint64_t get_timer()
{
    uint64_t ticks = get_ticks();
    asserts(time_base, "time base uninitialized");
    return ticks / time_base;
}

uint64_t get_time_base()
{
    return time_base;
}

void latency(uint64_t time)
{
    uint64_t begin_time = get_timer();

    while (get_timer() - begin_time < time);
    return;
}

void show_timer(void)
{
    uint64_t ticks = get_ticks();
    uint64_t base = get_time_base();
    printk("timer: ticks=%d, time_base=%d\n", ticks, base);
}

void check_sleeping(void)
{
    // TODO: [p2-task3] Pick out tasks that should wake up from the sleep queue

    uint64_t current_time = get_timer();
    // pretty_log(LOG_INFO, "current_time: %d", current_time);
    for (list_node_t *cur = sleep_queue.head.next, *next = NULL; cur != &sleep_queue.head; cur = next) {
        next = cur->next;
        pcb_t* pcb = container_of(cur, pcb_t, sched_node);
        // pretty_log(LOG_INFO, "checking pid %d with wakeup_time %d", pcb->pid, pcb->wakeup_time);
        if (current_time >= pcb->wakeup_time) {
            list_delete(cur);
            list_append(&ready_queue, cur);
        }
    }
    // breakpoint();
}
