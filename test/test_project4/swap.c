#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#define PAGE_SIZE     4096
#define USER_STACK_ADDR 0xf00010000
#define BASE_ADDR     USER_STACK_ADDR + PAGE_SIZE

volatile uint32_t touch_page(int page_idx) {
    volatile uint32_t* addr = (void*)(BASE_ADDR + page_idx * PAGE_SIZE);
    return *addr;
}

void delay(int n) {
    for (volatile int i = 0; i < n * 100000; i++);
}

int main() {
    sys_set_max_memory(PAGE_SIZE * 64);
    sys_sleep(10);
    sys_set_max_memory(PAGE_SIZE * (11 + 6)); // 11: basic pages
    printf("test swap...\n");
    int test_sequence[] = {0, 1, 2, 3, 4, 5, 6, 7, 0, 0, 2, 1, 3, 0, 0, 4, 0, 1, 5, 2, 3, 6, 0, 0, 3, 5, 0, 2, 1, 7, 0, 4};
    int total = sizeof(test_sequence) / sizeof(test_sequence[0]), swap = 0, page_fault = 0;
    uint64_t sum_ticks = 0;
    for (int i = 0; i < total; i++) {
        printf("[%d]", test_sequence[i]);
        uint64_t ticks = sys_get_proc_tick();
        touch_page(test_sequence[i]);
        ticks = sys_get_proc_tick() - ticks;
        // printf("%ld  ", ticks);
        if (ticks > 2000) {
            page_fault++;
            printf("miss  ");
        } else {
            printf("      ");
        }
        sum_ticks += ticks;
        if ((i + 1) % 8 == 0) printf("\n");
        delay(100);
    }
    printf("pagefault: %d/%d\n", page_fault, total);
    // printf("swapped: %d/%d\n", swap, total);
    printf("cputime: %lu ticks\n", sum_ticks);
    while (1);
    return 0;
}
