#include <os/mm.h>
#include <os/task.h>
#include <pgtable.h>

pageframe_group_t page_groups[NUM_MAX_TASK];
pageframe_group_t* const PAGE_GROUP_KERNEL = &page_groups[0];

pageframe_group_t* find_pageframe_group(kva_t page) {
    if (page < INIT_KERNEL_STACK) {
        return PAGE_GROUP_KERNEL;
    } else {
        pageframe_t* attr = get_page_attr(page);
        asserts(attr->group_node.container, "find_pageframe_group: isolated pageframe");
        return container_of(attr->group_node.container, pageframe_group_t, pages);
    }
}

void init_pageframe_group() {
    *PAGE_GROUP_KERNEL = (pageframe_group_t){
        .capacity = MAX_PAGE_NUM,
        .used = 0,
        .refcount = 1,
    };
    list_init(&PAGE_GROUP_KERNEL->pages, "init");
}

void show_pagegroups() {
    for (int i = 0; i < NUM_MAX_TASK; i++) {
        if (!page_groups[i].refcount) continue;
        printk("group %d: name=%s, capacity=%d, used=%d, refcount=%d\n", i,
               page_groups[i].pages.name, page_groups[i].capacity, page_groups[i].used,
               page_groups[i].refcount);
    }
}
