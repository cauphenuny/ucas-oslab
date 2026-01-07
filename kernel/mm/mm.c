#include <csr.h>
#include <logger.h>
#include <os/mm.h>
#include <os/sched.h>

kva_t new_top_pgdir(pageframe_group_t* group) {
    kva_t pgdir = alloc_pageframe(group, 1);
    clear_pgdir(pgdir);
    group->refcount++;
    return pgdir;
}

void bind_addr(PTE* pte, pa_t addr, uint64_t extra_attrs) {
    set_pfn(pte, addr >> NORMAL_PAGE_SHIFT);
    set_attribute(pte, _PAGE_PRESENT);
    set_attribute(pte, extra_attrs);
    local_flush_tlb_all();
}

kva_t bind_page(PTE* pte, kva_t page, uint64_t extra_attrs) {
    bind_addr(pte, kva2pa(page), extra_attrs);
    pageframe_t* attr = pageframe_kva2attr(page);
    attr->pte = pte;
    pretty_logd("bind addr 0x%x to pte 0x%x", kva2pa(page), pte);
    return page;
}

static inline kva_t add_page(uint64_t vpn, kva_t pgdir, uint64_t extra_attrs) {
    pageframe_group_t* group = find_pagegroup(pgdir);
    ptr_t new_page = alloc_pageframe(group, 1);
    PTE* pte = (PTE*)pgdir;
    return bind_page(&pte[vpn], new_page, extra_attrs);
}

/* this is used for mapping kernel virtual address into user page table */
void share_pgtable(kva_t dest_pgdir, kva_t src_pgdir) {
    // DONE [P4-task1] share_pgtable:
    PTE* src = (PTE*)src_pgdir;
    PTE* dest = (PTE*)dest_pgdir;
    for (int i = 0; i < PTE_ENTRY_NUM; i++) {
        PTE src_entry = src[i];
        if (get_attribute(src_entry, _PAGE_PRESENT)) {
            asserts(
                !get_attribute(dest[i], _PAGE_PRESENT),
                "share_pgtable: dest entry already present");
            if (get_attribute(src_entry, _PAGE_READ | _PAGE_WRITE | _PAGE_EXEC)) {
                // leaf entry
                // pretty_logd(
                //     "mapping leaf entry va idx %x pa 0x%x", i,
                //     get_pa(src_entry));
                dest[i] = src_entry;
            } else {
                // non-leaf entry
                // pretty_logd("mapping non-leaf entry va idx %x", i);
                kva_t new_page = add_page(i, (kva_t)dest, 0);
                clear_pgdir(new_page);
                share_pgtable(new_page, pa2kva(get_pa(src_entry)));
            }
        }
    }
}

PTE* find_pte(uva_t va, kva_t pgdir, bool create) {
    uint64_t vpn2, vpn1, vpn0;
    get_vpn(va, &vpn2, &vpn1, &vpn0);
    PTE* current_pgdir = (PTE*)pgdir;
    if (!get_attribute(current_pgdir[vpn2], _PAGE_PRESENT)) {
        if (create) {
            clear_pgdir(add_page(vpn2, (kva_t)current_pgdir, 0));
        } else {
            return NULL;
        }
    }
    current_pgdir = (PTE*)pa2kva(get_pa(current_pgdir[vpn2]));
    if (!get_attribute(current_pgdir[vpn1], _PAGE_PRESENT)) {
        if (create) {
            clear_pgdir(add_page(vpn1, (kva_t)current_pgdir, 0));
        } else {
            return NULL;
        }
    }
    current_pgdir = (PTE*)pa2kva(get_pa(current_pgdir[vpn1]));
    return &current_pgdir[vpn0];
}

PTE* find_kernel_pte(uva_t va, bool create, int num_pgdirs) {
    uint64_t vpn[3];
    get_vpn(va, &vpn[0], &vpn[1], &vpn[2]);
    PTE* current = (PTE*)PGDIR_VA;
    for (int i = 0; i < num_pgdirs - 1; i++) {
        if (!get_attribute(current[vpn[i]], _PAGE_PRESENT)) {
            if (create) {
                clear_pgdir(add_page(vpn[i], (kva_t)current, 0));
            } else {
                return NULL;
            }
        }
        current = (PTE*)pa2kva(get_pa(current[vpn[i]]));
    }
    return &current[vpn[num_pgdirs - 1]];
}

PTE* alloc_page(uva_t va, kva_t pgdir, bool exist_ok) {
    PTE* pte = find_pte(va, pgdir, true);
    if (*pte != 0) {
        asserts(exist_ok, "page already allocated");
        if (!get_attribute(*pte, _PAGE_PRESENT)) {
            asserts(get_attribute(*pte, _PAGE_SOFT), "invalid pte state");
            kva_t new_page = alloc_pageframe(find_pagegroup(pgdir), 1);
            swapin(va, pgdir, new_page);
        }
        return pte;
    } else {
        kva_t new_page = alloc_pageframe(find_pagegroup(pgdir), 1);
        bind_page(pte, new_page, _PAGE_USER | _PAGE_READ | _PAGE_WRITE | _PAGE_EXEC);
        pageframe_t* attr = pageframe_kva2attr(new_page);
        attr->uva = va;

        uint64_t vpn2, vpn1, vpn0;
        get_vpn(va, &vpn2, &vpn1, &vpn0);
        pretty_logn(
            "va 0x%lx(%x,%x,%x) in 0x%x mapped to new page 0x%x", va, vpn2, vpn1, vpn0,
            kva2pa(pgdir), kva2pa(new_page));

        return pte;
    }
}

kva_t uva2kva(uva_t uva, kva_t pgdir) {
    PTE* pte = alloc_page(uva, pgdir, true);  // make sure page is allocated
    return pa2kva(get_pa(*pte)) + (uva & (PAGE_SIZE - 1));
}

void memcpy_kva2uva(uva_t dest_va, kva_t src, size_t size, kva_t pgdir_dest) {
    while (size) {
        kva_t dest_kva = uva2kva(dest_va, pgdir_dest);
        kva_t dest_page_end = ((dest_kva >> NORMAL_PAGE_SHIFT) + 1) << NORMAL_PAGE_SHIFT;
        size_t capacity = dest_page_end - dest_kva;
        size_t active = min(size, capacity);
        // pretty_logd(
        //     "copying %d bytes from %lx to uva %lx (pa %x)", active, src, dest_va,
        //     kva2pa(dest_kva));
        memcpy((void*)dest_kva, (void*)src, active);
        size -= active;
        dest_va += active;
        src += active;
    }
}

void memcpy_uva2kva(kva_t dest, uva_t src_va, size_t size, kva_t pgdir_src) {
    while (size) {
        kva_t src_kva = uva2kva(src_va, pgdir_src);
        kva_t src_page_end = ((src_kva >> NORMAL_PAGE_SHIFT) + 1) << NORMAL_PAGE_SHIFT;
        size_t capacity = src_page_end - src_kva;
        size_t active = min(size, capacity);
        // pretty_logd(
        //     "copying %d bytes from uva %lx (pa %x) to %lx", active, src_va,
        //     kva2pa(src_kva), dest);
        memcpy((void*)dest, (void*)src_kva, active);
        size -= active;
        src_va += active;
        dest += active;
    }
}

void strcpy_kva2uva(uva_t dest_va, const char* src, kva_t pgdir_dest) {
    // pretty_logd("strcpy to uva %lx from src %lx", dest_va, (kva_t)src);
    size_t len = strlen(src) + 1;
    memcpy_kva2uva(dest_va, (kva_t)src, len, pgdir_dest);
}

static void free_pgdir(kva_t pgdir) {
    for (int i = 0; i < PTE_ENTRY_NUM; i++) {
        PTE pte = ((PTE*)pgdir)[i];
        if (get_attribute(pte, _PAGE_PRESENT)) {
            if (get_attribute(pte, _PAGE_READ | _PAGE_WRITE | _PAGE_EXEC)) {
                if (get_attribute(pte, _PAGE_USER)) {
                    free_pageframe(pa2kva(get_pa(pte)));
                }
            } else {
                free_pgdir(pa2kva(get_pa(pte)));
            }
        } else if (get_attribute(pte, _PAGE_SOFT)) {
            uint64_t swap_id = get_pfn(pte);
            free_swap(swap_id);
        }
    }
    free_pageframe(pgdir);
}

void free_top_pgdir(kva_t pgdir) {
    pageframe_group_t* group = find_pagegroup(pgdir);
    free_pgdir(pgdir);
    group->refcount--;
    if (!group->refcount) {
        free_pagegroup(group);
    }
}

// NOTE: this function is dangerous, make sure pcb is not running
void cleanup_vm(pcb_t* pcb) {
    free_top_pgdir(pcb->pgdir);
    free_pageframe(pcb->kernel_stack_bottom);
}

void init_vm() { pretty_logi("capacity: %d pages", MAX_PAGE_NUM); }
