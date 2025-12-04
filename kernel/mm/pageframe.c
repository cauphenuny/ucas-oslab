#include <csr.h>
#include <logger.h>
#include <os/mm.h>
#include <os/sched.h>

// NOTE: A/C-core
static ptr_t cur_kernel_mem = FREEMEM_KERNEL;

pageframe_t pages[MAX_PAGE_NUM];

static int pageframe_id(ptr_t addr) { return (addr - FREEMEM_KERNEL) / PAGE_SIZE; }

static ptr_t pageframe_addr(int id) { return FREEMEM_KERNEL + id * PAGE_SIZE; }

ptr_t alloc_pageframe(int num_page) {
    // align PAGE_SIZE
    int counter = 0;
    while (counter < MAX_PAGE_NUM) {
        ptr_t ret = ROUND(cur_kernel_mem, PAGE_SIZE);
        int id = pageframe_id(ret);
        bool available = true;
        for (int i = 0; i < num_page; i++) {
            if (id + i >= MAX_PAGE_NUM || pages[id + i].start) {
                available = false;
                break;
            }
        }
        if (available) {
            cur_kernel_mem += num_page * PAGE_SIZE;
            for (int i = 0; i < num_page; i++) {
                pages[id + i].start = ret;
            }
            return ret;
        } else {
            cur_kernel_mem += PAGE_SIZE;
            if (cur_kernel_mem >= ALLMEM_KERNEL) {
                cur_kernel_mem = FREEMEM_KERNEL;
            }
        }
        counter++;
    }
    asserts(false, "allocPage: out of memory");
    return 0;
}

void free_pageframe(ptr_t base_addr) {
    int id = pageframe_id(base_addr);
    if (id < 0) {
        pretty_loge("try to free kernel page");
        return;
    }
    asserts(id >= 0 && id < MAX_PAGE_NUM, "free_page: invalid addr");
    asserts(pages[id].start, "free_page: double free detected");
    ptr_t entry = pages[id].start;
    int entry_id = pageframe_id(pages[id].start);
    asserts(entry_id <= id, "free_page: corrupted start_addr");
    pretty_logd("free page block at addr 0x%x", kva2pa(entry));
    for (int i = entry_id; pages[i].start == entry; i = (i + 1) % MAX_PAGE_NUM) {
        pretty_logd("  free page #%d", i);
        pages[i].start = 0;
    }
}
