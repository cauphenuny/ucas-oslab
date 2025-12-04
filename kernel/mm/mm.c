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

kva_t bind_page(PTE* pte, kva_t page, uint64_t extra_attrs) {
    set_pfn(pte, kva2pa(page) >> NORMAL_PAGE_SHIFT);
    set_attribute(pte, _PAGE_PRESENT);
    set_attribute(pte, extra_attrs);
    pageframe_t* attr = get_page_attr(page);
    attr->pte = pte;
    pretty_logd("bind page 0x%x to pte 0x%x", kva2pa(page), pte);
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
            asserts(false, "find_pte: vpn2 not present");
        }
    }
    current_pgdir = (PTE*)pa2kva(get_pa(current_pgdir[vpn2]));
    if (!get_attribute(current_pgdir[vpn1], _PAGE_PRESENT)) {
        if (create) {
            clear_pgdir(add_page(vpn1, (kva_t)current_pgdir, 0));
        } else {
            asserts(false, "find_pte: vpn1 not present");
        }
    }
    current_pgdir = (PTE*)pa2kva(get_pa(current_pgdir[vpn1]));
    return &current_pgdir[vpn0];
}

PTE* alloc_page_va(uva_t va, kva_t pgdir) {
    PTE* pte = find_pte(va, pgdir, true);
    asserts(*pte == 0, "alloc_page_va: page already allocated");
    kva_t new_page = alloc_pageframe(find_pagegroup(pgdir), 1);
    bind_page(pte, new_page, _PAGE_USER | _PAGE_READ | _PAGE_WRITE | _PAGE_EXEC);

    uint64_t vpn2, vpn1, vpn0;
    get_vpn(va, &vpn2, &vpn1, &vpn0);
    pretty_logd(
        "va 0x%lx(%x,%x,%x) mapped to new page 0x%x", va, vpn2, vpn1, vpn0, kva2pa(new_page));

    return pte;
}

PTE* bind_page_va(uva_t va, kva_t pgdir, kva_t page) {
    PTE* pte = find_pte(va, pgdir, false);
    asserts(!get_attribute(*pte, _PAGE_PRESENT), "bind_page_va: pte already occupied");
    bind_page(pte, page, _PAGE_USER | _PAGE_EXEC | _PAGE_READ | _PAGE_WRITE);

    uint64_t vpn2, vpn1, vpn0;
    get_vpn(va, &vpn2, &vpn1, &vpn0);
    pretty_logd("va 0x%lx(%x,%x,%x) bound to page 0x%x", va, vpn2, vpn1, vpn0, kva2pa(page));

    return pte;
}

kva_t shm_page_get(int key) {
    // TODO [P4-task4] shm_page_get:
}

void shm_page_dt(kva_t addr) {
    // TODO [P4-task4] shm_page_dt:
}

kva_t uva2kva(uva_t uva, kva_t pgdir) {
    uva &= VA_MASK;
    uint64_t vpn2 = (uva >> (NORMAL_PAGE_SHIFT + PPN_BITS + PPN_BITS)) & VPN_MASK;
    uint64_t vpn1 = (uva >> (NORMAL_PAGE_SHIFT + PPN_BITS)) & VPN_MASK;
    uint64_t vpn0 = (uva >> NORMAL_PAGE_SHIFT) & VPN_MASK;
    PTE* current_pgdir = (PTE*)pgdir;
    if (!get_attribute(current_pgdir[vpn2], _PAGE_PRESENT)) goto not_exist;
    current_pgdir = (PTE*)pa2kva(get_pa(current_pgdir[vpn2]));
    if (!get_attribute(current_pgdir[vpn1], _PAGE_PRESENT)) goto not_exist;
    current_pgdir = (PTE*)pa2kva(get_pa(current_pgdir[vpn1]));
    if (!get_attribute(current_pgdir[vpn0], _PAGE_PRESENT)) goto not_exist;
    kva_t page_base = pa2kva(get_pa(current_pgdir[vpn0]));
    return page_base + (uva & (PAGE_SIZE - 1));
not_exist:
    alloc_page_va(uva, pgdir);
    return uva2kva(uva, pgdir);
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
        }
    }
    free_pageframe(pgdir);
}

static void free_top_pgdir(kva_t pgdir) {
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
