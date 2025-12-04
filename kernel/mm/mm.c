#include <os/sched.h>
#include <csr.h>
#include <logger.h>
#include <os/mm.h>

ptr_t new_pgdir() {
    ptr_t pgdir = alloc_pageframe(1);
    clear_pgdir(pgdir);
    return pgdir;
}

size_t get_free_memory() {
    size_t free_mem = 0;
    for (int i = 0; i < MAX_PAGE_NUM; i++) {
        if (pages[i].start == 0) {
            free_mem += PAGE_SIZE;
        }
    }
    return free_mem;
}

static inline uintptr_t add_page(uint64_t vpn, PTE* pgdir, uint64_t extra_attrs) {
    ptr_t new_page = alloc_pageframe(1);
    set_pfn(&pgdir[vpn], kva2pa(new_page) >> NORMAL_PAGE_SHIFT);
    set_attribute(&pgdir[vpn], _PAGE_PRESENT);
    set_attribute(&pgdir[vpn], extra_attrs);
    return new_page;
}

/* this is used for mapping kernel virtual address into user page table */
void share_pgtable(uintptr_t dest_pgdir, uintptr_t src_pgdir) {
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
                pretty_logd(
                    "mapping leaf entry va idx %x pa 0x%x", i,
                    get_pa(src_entry));
                dest[i] = src_entry;
            } else {
                // non-leaf entry
                pretty_logd("mapping non-leaf entry va idx %x", i);
                uintptr_t new_page = add_page(i, dest, 0);
                share_pgtable(new_page, pa2kva(get_pa(src_entry)));
            }
        }
    }
}

// NOTE: does this func need a `mask` to specify attributes?

/* allocate physical page for `va`, mapping it into `pgdir`,
   return the kernel virtual address for the page
   */
uintptr_t alloc_page_va(uintptr_t va, uintptr_t pgdir) {
    va &= VA_MASK;
    uint64_t vpn2 = (va >> (NORMAL_PAGE_SHIFT + PPN_BITS + PPN_BITS)) & VPN_MASK;
    uint64_t vpn1 = (va >> (NORMAL_PAGE_SHIFT + PPN_BITS)) & VPN_MASK;
    uint64_t vpn0 = (va >> NORMAL_PAGE_SHIFT) & VPN_MASK;
    PTE* current_pgdir = (PTE*)pgdir;
    if (current_pgdir[vpn2] == 0) clear_pgdir(add_page(vpn2, current_pgdir, 0));
    current_pgdir = (PTE*)pa2kva(get_pa(current_pgdir[vpn2]));
    if (current_pgdir[vpn1] == 0) clear_pgdir(add_page(vpn1, current_pgdir, 0));
    current_pgdir = (PTE*)pa2kva(get_pa(current_pgdir[vpn1]));
    asserts(current_pgdir[vpn0] == 0, "alloc_page_helper: page already allocated");
    uintptr_t new_page =
        add_page(vpn0, current_pgdir, _PAGE_USER | _PAGE_READ | _PAGE_WRITE | _PAGE_EXEC);
    pretty_logd(
        "va 0x%lx(%x,%x,%x) mapped to new page 0x%x", va, vpn2, vpn1, vpn0, kva2pa(new_page));
    return new_page;
}

uintptr_t shm_page_get(int key) {
    // TODO [P4-task4] shm_page_get:
}

void shm_page_dt(uintptr_t addr) {
    // TODO [P4-task4] shm_page_dt:
}

uintptr_t uva2kva(uintptr_t uva, uintptr_t pgdir) {
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
    uintptr_t page_base = pa2kva(get_pa(current_pgdir[vpn0]));
    return page_base + (uva & (PAGE_SIZE - 1));
not_exist:
    alloc_page_va(uva, pgdir);
    return uva2kva(uva, pgdir);
}

void memcpy_kva2uva(uintptr_t dest_va, uintptr_t src, size_t size, uintptr_t pgdir_dest) {
    while (size) {
        uintptr_t dest_kva = uva2kva(dest_va, pgdir_dest);
        uintptr_t dest_page_end = ((dest_kva >> NORMAL_PAGE_SHIFT) + 1) << NORMAL_PAGE_SHIFT;
        size_t capacity = dest_page_end - dest_kva;
        size_t active = min(size, capacity);
        pretty_logd(
            "copying %d bytes from %lx to uva %lx (pa %x)", active, src, dest_va, kva2pa(dest_kva));
        memcpy((void*)dest_kva, (void*)src, active);
        size -= active;
        dest_va += active;
        src += active;
    }
}

void strcpy_kva2uva(uintptr_t dest_va, const char* src, uintptr_t pgdir_dest) {
    pretty_logd("strcpy to uva %lx from src %lx", dest_va, (uintptr_t)src);
    size_t len = strlen(src) + 1;
    memcpy_kva2uva(dest_va, (uintptr_t)src, len, pgdir_dest);
}

static void free_pgdir(uintptr_t pgdir) {
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

// NOTE: this function is dangerous, make sure pcb is not running
void cleanup_vm(pcb_t* pcb) {
    free_pgdir(pcb->pgdir);
    free_pageframe(pcb->kernel_stack_bottom);
}

void init_vm() {
    pretty_logi("capacity: %d pages", MAX_PAGE_NUM);
}
