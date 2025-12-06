#include "tui.hpp"

extern "C" {
#include <logger.h>
#include <os/mm.h>
#include <os/task.h>
#include <os/time.h>
#include <pgtable.h>

pageframe_group_t page_groups[NUM_MAX_PAGEGROUP];
pageframe_group_t* const PAGE_GROUP_KERNEL = &page_groups[0];

pageframe_group_t* find_pagegroup(kva_t page) {
    if (page == PGDIR_VA) return PAGE_GROUP_KERNEL;
    asserts(page >= INIT_KERNEL_STACK && page <= ALLMEM_KERNEL, "invalid pageframe address");
    pageframe_t* attr = pageframe_kva2attr(page);
    asserts(attr->group_node.container, "isolated pageframe");
    return container_of(attr->group_node.container, pageframe_group_t, pages);
}

void init_pagegroup() {
    *PAGE_GROUP_KERNEL = (pageframe_group_t){
        .capacity = MAX_PAGE_NUM,
        .used = 0,
        .refcount = 1,
        .vtable = PAGEGROUP_VTABLE_SC,
    };
    list_init(&PAGE_GROUP_KERNEL->pages, "init");
}

void show_pagegroup_details(int pgid) {
    pageframe_group_t* group = &page_groups[pgid];
    printk(
        "group #%d '%s': algo=%s, capacity=%d, used=%d, refcount=%d\n", pgid, group->pages.name,
        group->vtable->name, group->capacity, group->used, group->refcount);
    list_foreach_node(iter, &group->pages.head) {
        pageframe_t* pf = container_of(iter, pageframe_t, group_node);
        group->vtable->show(group, pf);
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
    using T = pageframe_group_t;
    display_table<T>(
        page_groups, NUM_MAX_PAGEGROUP, [](T* group) { return group->refcount > 0; },
        table_entry_t{"ID", 4, [](T* group) { printk("%d", group - page_groups); }},
        table_entry_t{"NAME", 16, [](T* group) { printk("%s", group->pages.name); }},
        table_entry_t{"ALGO", 6, [](T* group) { printk("%s", group->vtable->name); }},
        table_entry_t{"CAPACITY", 10, [](T* group) { printk("%d", group->capacity); }},
        table_entry_t{"USED", 6, [](T* group) { printk("%d", group->used); }},
        table_entry_t{"REF", 5, [](T* group) { printk("%d", group->refcount); }});
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
    for (int i = 0; i < (int)PTE_ENTRY_NUM; i++) {
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
    new_group->vtable = group->vtable;

    group->refcount--;
    group->capacity -= capacity;
    shrink_pagegroup(group, 0);

    pretty_logi(
        "immigrating pgdir 0x%x from group '%s' to group '%s'", kva2pa(top_pgdir),
        group->pages.name, new_group->pages.name);
    immigrate(top_pgdir, group, new_group);
    if (new_group->vtable->init) {
        new_group->vtable->init(new_group);
    }

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
    asserts(group->used == 0, "group is not empty");
    PAGE_GROUP_KERNEL->capacity += group->capacity;
    pretty_logi("freed pagegroup '%s'", group->pages.name);
}
}