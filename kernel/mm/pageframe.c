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
    asserts(false, "alloc_pageframe: out of memory");
    return 0;
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
    pageframe_group_t* group = find_pagegroup(entry);
    for (int i = entry_id; attrs[i].start == entry; i = (i + 1) % MAX_PAGE_NUM) {
        attrs[i].start = 0;
        detach_pageframe(pageframe_addr(i), group);
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

void attach_pageframe(kva_t page, pageframe_group_t* group) {
    shrink_pagegroup(group, 1);
    int id = pageframe_id(page);
    list_prepend(&group->pages, &pages[id].group_node);
    group->used++;
    pretty_logd("attached page 0x%x to group '%s'", kva2pa(page), group->pages.name);
}

void detach_pageframe(kva_t page, pageframe_group_t* group) {
    int id = pageframe_id(page);
    list_delete(&pages[id].group_node);
    group->used--;
    pretty_logd("detached page 0x%x from group '%s'", kva2pa(page), group->pages.name);
}

void maintain_pagelist_lru(pageframe_group_t* group, uint64_t current_tick) {
    list_foreach_node(iter, &group->pages.head) {
        pageframe_t* pf = container_of(iter, pageframe_t, group_node);
        if (pf->pte && get_attribute(*pf->pte, _PAGE_EXEC | _PAGE_READ | _PAGE_WRITE)) {
            if (!get_attribute(*pf->pte, _PAGE_ACCESSED)) {
                continue;
            }
        }
        pf->last_accessed = current_tick;
        list_delete(iter);
        list_prepend(&group->pages, iter);
        if (pf->pte && get_attribute(*pf->pte, _PAGE_EXEC | _PAGE_READ | _PAGE_WRITE)) {
            clear_attribute(pf->pte, _PAGE_ACCESSED);
        }
    }
}

void maintain_pagelist_fifo(pageframe_group_t* group, uint64_t current_tick) {
    list_foreach_node(iter, &group->pages.head) {
        pageframe_t* pf = container_of(iter, pageframe_t, group_node);
        if (pf->pte && get_attribute(*pf->pte, _PAGE_EXEC | _PAGE_READ | _PAGE_WRITE)) {
            if (!get_attribute(*pf->pte, _PAGE_ACCESSED)) {
                continue;
            }
        }
        pf->last_accessed = current_tick;
        if (pf->pte && get_attribute(*pf->pte, _PAGE_EXEC | _PAGE_READ | _PAGE_WRITE)) {
            clear_attribute(pf->pte, _PAGE_ACCESSED);
        }
    }
}

void pageframe_destruct(pageframe_t* pf, kva_t addr, pageframe_group_t* group) {
    pf->pte = NULL;
    pf->last_accessed = 0;
    free_pageframe(addr);
}
