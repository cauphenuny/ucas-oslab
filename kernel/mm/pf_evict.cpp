extern "C" {
#include <logger.h>
#include <os/list.h>
#include <os/mm.h>
}

namespace fifo {

static void attach(pageframe_group_t* group, int pageframe_id) {
    shrink_pagegroup(group, 1);
    list_prepend(&group->pages, &pages[pageframe_id].group_node);
    group->used++;
    pretty_logd("attached page #%d to group '%s'", pageframe_id, group->pages.name);
}

static void detach(pageframe_group_t* group, int pageframe_id) {
    list_delete(&pages[pageframe_id].group_node);
    group->used--;
    pretty_logd("detached page #%d from group '%s'", pageframe_id, group->pages.name);
}

static void on_access(pageframe_group_t* group, kva_t pageframe, uint64_t current_tick) {
    pageframe_t* pf = pageframe_kva2attr(pageframe);
    set_attribute(pf->pte, _PAGE_ACCESSED);
}

static void on_write(pageframe_group_t* group, kva_t pageframe, uint64_t current_tick) {
    pageframe_t* pf = pageframe_kva2attr(pageframe);
    set_attribute(pf->pte, _PAGE_DIRTY | _PAGE_ACCESSED);
}

static list_node_t* evict(pageframe_group_t* group) {
    // NOTE: find a leaf page in reverse order
    list_node_t* node = NULL;
    list_foreach_node_reversed(iter, &group->pages.head) {
        pageframe_t* pf = container_of(iter, pageframe_t, group_node);
        PTE* pte = pf->pte;
        if (pte && get_attribute(*pte, _PAGE_EXEC | _PAGE_READ | _PAGE_WRITE)) {
            node = iter;
            break;
        }
    }
    if (!node) {
        pretty_loge("no leaf page to evict in group '%s'", group->pages.name);
        return NULL;
    }
    // NOTE: move trailing pgdir pages to the front
    list_foreach_node_reversed(iter, &group->pages.head) {
        if (iter == node) {
            break;
        }
        list_delete(iter);
        list_prepend(&group->pages, iter);
    }
    return node;
}

static void show(pageframe_group_t* group, pageframe_t* pf) {
    kva_t page = pageframe_attr2kva(pf);
    if (!pf->pte) {
        if (group == PAGE_GROUP_KERNEL)
            printk("  page 0x%x: pagedir (top) or kernel page\n", kva2pa(page));
        else
            printk("  page 0x%x: pagedir (top)\n", kva2pa(page));
    } else if (!get_attribute(*pf->pte, _PAGE_EXEC | _PAGE_READ | _PAGE_WRITE)) {
        printk("  page 0x%x: pagedir (intermediate)\n", kva2pa(page));
    } else {
        printk("  page 0x%x: leaf\n", kva2pa(page));
    }
}

static pagegroup_vtable_t VTABLE = {
    .init = nullptr,
    .cleanup = nullptr,
    .attach = attach,
    .detach = detach,

    .on_timer = nullptr,
    .on_access = on_access,
    .on_write = on_write,

    .evict = evict,
    .show = show,

    .name = "FIFO",
};

}  // namespace fifo

namespace second_chance {

static list_node_t* evict(pageframe_group_t* group) {
    list_node_t* victim = fifo::evict(group);
    if (!victim) return NULL;  // no leaf page found
    while (true) {
        list_node_t* iter = group->pages.head.prev;  // NOTE: in reverse order
        pageframe_t* pf = container_of(iter, pageframe_t, group_node);
        PTE* pte = pf->pte;
        if (pte && get_attribute(*pte, _PAGE_EXEC | _PAGE_READ | _PAGE_WRITE)) {
            if (get_attribute(*pte, _PAGE_ACCESSED)) {
                clear_attribute(pte, _PAGE_ACCESSED);
                list_delete(iter);
                list_prepend(&group->pages, iter);
            } else {
                // found victim
                return iter;
            }
        } else {
            // non-leaf page, move to the back directly
            list_delete(iter);
            list_prepend(&group->pages, iter);
        }
    }
}

static void show(pageframe_group_t* group, pageframe_t* pf) {
    if (pf->pte && get_attribute(*pf->pte, _PAGE_EXEC | _PAGE_READ | _PAGE_WRITE)) {
        printk(
            "  page 0x%x: leaf, accessed=%lu\n", kva2pa(pageframe_attr2kva(pf)),
            get_attribute(*pf->pte, _PAGE_ACCESSED));
    } else {
        fifo::show(group, pf);
    }
}

static pagegroup_vtable_t VTABLE = {
    .init = nullptr,
    .cleanup = nullptr,
    .attach = fifo::attach,
    .detach = fifo::detach,

    .on_timer = nullptr,
    .on_access = fifo::on_access,
    .on_write = fifo::on_write,

    .evict = evict,
    .show = show,

    .name = "LRU",
};

}  // namespace second_chance

namespace lru {

uint64_t last_accessed[MAX_PAGE_NUM];

static void init(pageframe_group_t* group) {
    list_foreach_node(iter, &group->pages.head) {
        pageframe_t* pf = container_of(iter, pageframe_t, group_node);
        int id = pageframe_attr2id(pf);
        last_accessed[id] = 0;
    }
}

static void on_timer(pageframe_group_t* group, uint64_t current_tick) {
    list_foreach_node(iter, &group->pages.head) {
        pageframe_t* pf = container_of(iter, pageframe_t, group_node);
        int id = pageframe_attr2id(pf);
        if (pf->pte && get_attribute(*pf->pte, _PAGE_EXEC | _PAGE_READ | _PAGE_WRITE)) {
            if (!get_attribute(*pf->pte, _PAGE_ACCESSED)) {
                continue;
            }
        }
        last_accessed[id] = current_tick;
        list_delete(iter);
        list_prepend(&group->pages, iter);
        if (pf->pte && get_attribute(*pf->pte, _PAGE_EXEC | _PAGE_READ | _PAGE_WRITE)) {
            clear_attribute(pf->pte, _PAGE_ACCESSED);
        }
    }
}

static void on_access(pageframe_group_t* group, kva_t pageframe, uint64_t current_tick) {
    fifo::on_access(group, pageframe, current_tick);
    last_accessed[pageframe_kva2id(pageframe)] = current_tick;
}

static void on_write(pageframe_group_t* group, kva_t pageframe, uint64_t current_tick) {
    fifo::on_write(group, pageframe, current_tick);
    last_accessed[pageframe_kva2id(pageframe)] = current_tick;
}

static void show(pageframe_group_t* group, pageframe_t* pf) {
    if (pf->pte && get_attribute(*pf->pte, _PAGE_EXEC | _PAGE_READ | _PAGE_WRITE)) {
        int id = pageframe_attr2id(pf);
        printk(
            "  page 0x%x: leaf, last_accessed=%lu\n", kva2pa(pageframe_attr2kva(pf)),
            last_accessed[id]);
    } else {
        fifo::show(group, pf);
    }
}

static pagegroup_vtable_t VTABLE = {
    .init = init,
    .cleanup = nullptr,
    .attach = fifo::attach,
    .detach = fifo::detach,

    .on_timer = on_timer,
    .on_access = on_access,
    .on_write = on_write,

    .evict = fifo::evict,
    .show = show,

    .name = "SC",
};

}  // namespace lru

extern "C" {
pagegroup_vtable_t* const PAGEGROUP_VTABLE_FIFO = &fifo::VTABLE;
pagegroup_vtable_t* const PAGEGROUP_VTABLE_LRU = &lru::VTABLE;
pagegroup_vtable_t* const PAGEGROUP_VTABLE_SC = &second_chance::VTABLE;
}
