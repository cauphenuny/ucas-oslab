#include <csr.h>
#include <logger.h>
#include <os/mm.h>
#include <os/sched.h>

// NOTE: A/C-core
static ptr_t cur_kernel_mem = FREEMEM_KERNEL;

pageframe_t pages[MAX_PAGE_NUM];

typedef struct private_pf_attr {
    kva_t start;
} private_pf_attr_t;

static private_pf_attr_t attrs[MAX_PAGE_NUM];

kva_t alloc_pageframe(pageframe_group_t* group, int num_page) {
    if (!group) {
        pretty_loge("invalid pageframe group");
    }
    shrink_pagegroup(group, num_page);
    int counter = 0;
    while (counter < MAX_PAGE_NUM) {
        ptr_t ret = ROUND(cur_kernel_mem, PAGE_SIZE);
        int id = pageframe_kva2id(ret);
        bool available = true;
        for (int i = 0; i < num_page; i++) {
            if (id + i >= MAX_PAGE_NUM || attrs[id + i].start) {
                available = false;
                break;
            }
        }
        if (available) {
            cur_kernel_mem += num_page * PAGE_SIZE;
            for (int i = 0; i < num_page; i++) {
                attach_pageframe(ret + i * PAGE_SIZE, group);
                attrs[id + i].start = ret;
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
    halt("out of memory");
    return 0;
}

void free_pageframe(ptr_t base_addr) {
    int id = pageframe_kva2id(base_addr);
    if (id < 0) {
        pretty_loge("try to free kernel page");
        return;
    }
    asserts(id >= 0 && id < MAX_PAGE_NUM, "invalid addr");
    asserts(attrs[id].start, "double free detected");
    ptr_t entry = attrs[id].start;
    int entry_id = pageframe_kva2id(attrs[id].start);
    asserts(entry_id <= id, "corrupted start_addr");
    pretty_logd("free page block at addr 0x%x", kva2pa(entry));
    pageframe_group_t* group = find_pagegroup(entry);
    for (int i = entry_id; attrs[i].start == entry; i = (i + 1) % MAX_PAGE_NUM) {
        attrs[i].start = 0;
        detach_pageframe(pageframe_id2addr(i), group);
    }
}

size_t get_free_memory() {
    size_t free_mem = 0;
    for (int i = 0; i < MAX_PAGE_NUM; i++) {
        if (attrs[i].start == 0) {
            free_mem += PAGE_SIZE;
        }
    }
    return free_mem;
}

pageframe_t* pageframe_kva2attr(kva_t page) {
    int id = pageframe_kva2id(page);
    asserts(id >= 0 && id < MAX_PAGE_NUM, "invalid page addr");
    return &pages[id];
}

void pageframe_destruct(pageframe_t* pf, kva_t addr) {
    pf->pte = NULL;
    free_pageframe(addr);
}
