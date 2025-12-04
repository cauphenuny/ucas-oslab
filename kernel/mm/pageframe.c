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

static int pageframe_id(ptr_t addr) { return (addr - FREEMEM_KERNEL) / PAGE_SIZE; }

static ptr_t pageframe_addr(int id) { return FREEMEM_KERNEL + id * PAGE_SIZE; }

kva_t alloc_pageframe(pageframe_group_t* group, int num_page) {
    if (!group) {
        pretty_loge("invalid pageframe group");
    }
    if (group->used + num_page > group->capacity) {
        pretty_logw(
            "exceeding pageframe group %s's capacity: %d, used: %d, alloc: %d", group->pages.name,
            group->capacity, group->used, num_page);
        if (num_page > 1) {
            asserts(false, "alloc_pageframe: can not swapout multi-page");
            return 0;
        } else {
            return swapout(group);
        }
    } else {
        int counter = 0;
        while (counter < MAX_PAGE_NUM) {
            ptr_t ret = ROUND(cur_kernel_mem, PAGE_SIZE);
            int id = pageframe_id(ret);
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
                    attach_page(ret + i * PAGE_SIZE, group);
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
        asserts(false, "alloc_pageframe: out of memory");
        return 0;
    }
}

void free_pageframe(ptr_t base_addr) {
    int id = pageframe_id(base_addr);
    if (id < 0) {
        pretty_loge("try to free kernel page");
        return;
    }
    asserts(id >= 0 && id < MAX_PAGE_NUM, "free_page: invalid addr");
    asserts(attrs[id].start, "free_page: double free detected");
    ptr_t entry = attrs[id].start;
    int entry_id = pageframe_id(attrs[id].start);
    asserts(entry_id <= id, "free_page: corrupted start_addr");
    pretty_logd("free page block at addr 0x%x", kva2pa(entry));
    pageframe_group_t* group = find_pageframe_group(entry);
    for (int i = entry_id; attrs[i].start == entry; i = (i + 1) % MAX_PAGE_NUM) {
        attrs[i].start = 0;
        detach_page(pageframe_addr(i), group);
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

pageframe_t* get_page_attr(kva_t page) {
    int id = pageframe_id(page);
    asserts(id >= 0 && id < MAX_PAGE_NUM, "get_page_attr: invalid page addr");
    return &pages[id];
}

void attach_page(kva_t page, pageframe_group_t* group) {
    int id = pageframe_id(page);
    list_append(&group->pages, &pages[id].group_node);
    group->used++;
    pretty_logd("attached page 0x%x to group %s", kva2pa(page), group->pages.name);
}

void detach_page(kva_t page, pageframe_group_t* group) {
    int id = pageframe_id(page);
    list_delete(&pages[id].group_node);
    group->used--;
    pretty_logd("detached page 0x%x from group %s", kva2pa(page), group->pages.name);
}
