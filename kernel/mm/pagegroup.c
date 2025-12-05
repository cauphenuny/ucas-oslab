#include <logger.h>
#include <os/mm.h>
#include <os/task.h>
#include <os/time.h>
#include <pgtable.h>

pageframe_group_t page_groups[NUM_MAX_PAGEGROUP];
pageframe_group_t* const PAGE_GROUP_KERNEL = &page_groups[0];

pageframe_group_t* find_pagegroup(kva_t page) {
    if (page == PGDIR_VA) return PAGE_GROUP_KERNEL;
    asserts(
        page >= INIT_KERNEL_STACK && page <= ALLMEM_KERNEL,
        "find_pageframe_group: invalid pageframe address");
    pageframe_t* attr = get_page_attr(page);
    asserts(attr->group_node.container, "find_pageframe_group: isolated pageframe");
    return container_of(attr->group_node.container, pageframe_group_t, pages);
}

void init_pagegroup() {
    *PAGE_GROUP_KERNEL = (pageframe_group_t){
        .capacity = MAX_PAGE_NUM,
        .used = 0,
        .refcount = 1,
        .maintain = maintain_pagelist_lru,
    };
    list_init(&PAGE_GROUP_KERNEL->pages, "init");
}

void show_pagegroup_details(int pgid) {
    pageframe_group_t* group = &page_groups[pgid];
    printk(
        "group #%d '%s': capacity=%d, used=%d, refcount=%d\n", pgid, group->pages.name,
        group->capacity, group->used, group->refcount);
    uint64_t cur = get_ticks();
    list_foreach_node(iter, &group->pages.head) {
        pageframe_t* pf = container_of(iter, pageframe_t, group_node);
        kva_t page = pageframe_addr(pf - pages);
        if (!pf->pte) {
            if (group == PAGE_GROUP_KERNEL)
                printk("  page 0x%x: pagedir (top) or kernel page\n", kva2pa(page));
            else
                printk("  page 0x%x: pagedir (top)\n", kva2pa(page));
        } else if (!get_attribute(*pf->pte, _PAGE_EXEC | _PAGE_READ | _PAGE_WRITE)) {
            printk("  page 0x%x: pagedir (intermediate)\n", kva2pa(page));
        } else {
            printk("  page 0x%x: leaf: delta_t=%lu\n", kva2pa(page), cur - pf->last_accessed);
        }
    }
}

void show_pagegroup(const char* name) {
    int hit = 0;
    for (int i = 0; i < NUM_MAX_PAGEGROUP; i++) {
        if (!page_groups[i].refcount) continue;
        if (strcmp(page_groups[i].pages.name, name) == 0) {
            show_pagegroup_details(i);
            hit = 1;
        }
    }
    if (!hit) {
        pretty_logw("no such pageframe group '%s'", name);
    }
}

void show_pagegroups(int argc, char** argv) {
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            show_pagegroup(argv[i]);
        }
        return;
    }
    for (int i = 0; i < NUM_MAX_PAGEGROUP; i++) {
        if (!page_groups[i].refcount) continue;
        printk(
            "group #%d '%s': capacity=%d, used=%d, refcount=%d\n", i, page_groups[i].pages.name,
            page_groups[i].capacity, page_groups[i].used, page_groups[i].refcount);
    }
}

static pageframe_group_t* alloc_pageframe_group() {
    for (int i = 1; i < NUM_MAX_PAGEGROUP; i++) {
        if (page_groups[i].refcount == 0) {
            return &page_groups[i];
        }
    }
    pretty_loge("no free pageframe group");
    return NULL;
}

static void immigrate(kva_t pgdir, pageframe_group_t* group, pageframe_group_t* new_group) {
    shrink_pagegroup(new_group, 1);
    detach_pageframe(pgdir, group);
    attach_pageframe(pgdir, new_group);
    for (int i = 0; i < PTE_ENTRY_NUM; i++) {
        PTE pte = ((PTE*)pgdir)[i];
        // NOTE: only consider acvitve pages (ignore swapped-out pages)
        if (get_attribute(pte, _PAGE_PRESENT)) {
            kva_t page = pa2kva(get_pa(pte));
            if (get_attribute(pte, _PAGE_READ | _PAGE_WRITE | _PAGE_EXEC)) {
                if (get_attribute(pte, _PAGE_USER)) {
                    shrink_pagegroup(new_group, 1);
                    detach_pageframe(page, group);
                    attach_pageframe(page, new_group);
                }
            } else {
                immigrate(page, group, new_group);
            }
        }
    }
}

void shrink_pagegroup(pageframe_group_t* group, size_t space) {
    while (group->used + space > group->capacity) {
        swapout(group);
    }
}

int fork_pagegroup(kva_t top_pgdir, size_t capacity, const char* name) {
    pageframe_group_t* group = find_pagegroup(top_pgdir);
    if (capacity >= group->capacity) {
        pretty_loge(
            "original capacity %lu is less than new capacity %lu", group->capacity, capacity);
        return 1;
    }

    pageframe_group_t* new_group = alloc_pageframe_group();
    if (!new_group) {
        return 1;
    }
    memset(new_group, 0, sizeof(pageframe_group_t));
    new_group->capacity = capacity;
    new_group->refcount = 1;
    list_init(&new_group->pages, name);
    new_group->maintain = group->maintain;

    group->refcount--;
    group->capacity -= capacity;
    shrink_pagegroup(group, 0);

    pretty_logi("immigrating pgdir 0x%x from group '%s' to group '%s'", kva2pa(top_pgdir),
                group->pages.name, new_group->pages.name);
    immigrate(top_pgdir, group, new_group);

    pretty_logi(
        "forked new pagegroup '%s' with capacity %lu from group '%s'", name, capacity,
        group->pages.name);
    return 0;
}

int resize_pagegroup(pageframe_group_t* group, size_t new_capacity) {
    if (group == PAGE_GROUP_KERNEL) {
        pretty_loge("can not directly resize kernel pagegroup");
        return 1;
    }
    if (new_capacity > group->capacity) {
        size_t diff = new_capacity - group->capacity;
        if (PAGE_GROUP_KERNEL->capacity < diff) {
            pretty_loge(
                "not enough free memory in kernel pagegroup to resize, need %lu pages", diff);
            return 1;
        }
        PAGE_GROUP_KERNEL->capacity -= diff;
        shrink_pagegroup(PAGE_GROUP_KERNEL, 0);
        group->capacity += diff;
    } else if (new_capacity < group->capacity) {
        size_t diff = group->capacity - new_capacity;
        PAGE_GROUP_KERNEL->capacity += diff;
        group->capacity -= diff;
        shrink_pagegroup(group, 0);
    }
    pretty_logi("resized group '%s' to %lu", group->pages.name, new_capacity);
    return 0;
}

void free_pagegroup(pageframe_group_t* group) {
    asserts(group->used == 0, "free_pagegroup: group is not empty");
    PAGE_GROUP_KERNEL->capacity += group->capacity;
    pretty_logi("freed pagegroup '%s'", group->pages.name);
}
