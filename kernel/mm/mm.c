#include <os/mm.h>
#include <logger.h>

// NOTE: A/C-core
static ptr_t kernMemCurr = FREEMEM_KERNEL;

ptr_t allocPage(int numPage)
{
    // align PAGE_SIZE
    ptr_t ret = ROUND(kernMemCurr, PAGE_SIZE);
    kernMemCurr = ret + numPage * PAGE_SIZE;
    return ret;
}

// NOTE: Only need for S-core to alloc 2MB large page
#ifdef S_CORE
static ptr_t largePageMemCurr = LARGE_PAGE_FREEMEM;
ptr_t allocLargePage(int numPage)
{
    // align LARGE_PAGE_SIZE
    ptr_t ret = ROUND(largePageMemCurr, LARGE_PAGE_SIZE);
    largePageMemCurr = ret + numPage * LARGE_PAGE_SIZE;
    return ret;    
}
#endif

void freePage(ptr_t baseAddr)
{
    // TODO [P4-task1] (design you 'freePage' here if you need):
}

void *kmalloc(size_t size)
{
    // TODO [P4-task1] (design you 'kmalloc' here if you need):
}

static inline uintptr_t add_page(uint64_t vpn, PTE* pgdir)
{
    ptr_t new_page = allocPage(1);
    set_pfn(&pgdir[vpn], kva2pa(new_page) >> NORMAL_PAGE_SHIFT);
    set_attribute(&pgdir[vpn], _PAGE_PRESENT);
    return new_page;
}

/* this is used for mapping kernel virtual address into user page table */
void share_pgtable(uintptr_t dest_pgdir, uintptr_t src_pgdir)
{
    // DONE [P4-task1] share_pgtable:
    PTE* src = (PTE*)src_pgdir;
    PTE* dest = (PTE*)dest_pgdir;
    for (int i = 0; i < PTE_ENTRY_NUM; i++) {
        PTE src_entry = src[i];
        if (get_attribute(src_entry, _PAGE_PRESENT)) {
            asserts(!get_attribute(dest[i], _PAGE_PRESENT), "share_pgtable: dest entry already present");
            dest[i] = src_entry;
        }
    }
}

/* allocate physical page for `va`, mapping it into `pgdir`,
   return the kernel virtual address for the page
   */
uintptr_t alloc_page_helper(uintptr_t va, uintptr_t pgdir)
{
    // DONE [P4-task1] alloc_page_helper
    va &= VA_MASK;
    uint64_t vpn2 = va >> (NORMAL_PAGE_SHIFT + PPN_BITS + PPN_BITS);
    uint64_t vpn1 = (vpn2 << PPN_BITS) ^ (va >> (NORMAL_PAGE_SHIFT + PPN_BITS));
    uint64_t vpn0 = ((vpn2 << (PPN_BITS + PPN_BITS)) + (vpn1 << PPN_BITS)) ^ (va >> NORMAL_PAGE_SHIFT);
    PTE* current_pgdir = (PTE*)pgdir;
    if (current_pgdir[vpn2] == 0) clear_pgdir(add_page(vpn2, current_pgdir));
    current_pgdir = (PTE*)pa2kva(get_pa(current_pgdir[vpn2]));
    if (current_pgdir[vpn1] == 0) clear_pgdir(add_page(vpn1, current_pgdir));
    current_pgdir = (PTE*)pa2kva(get_pa(current_pgdir[vpn1]));
    asserts(current_pgdir[vpn0] == 0, "alloc_page_helper: page already allocated");
    uintptr_t new_page = add_page(vpn0, current_pgdir);
    pretty_log(LOG_INFO, "va 0x%lx mapped to new page 0x%lx", va, new_page);
    return new_page;
}

uintptr_t shm_page_get(int key)
{
    // TODO [P4-task4] shm_page_get:
}

void shm_page_dt(uintptr_t addr)
{
    // TODO [P4-task4] shm_page_dt:
}
