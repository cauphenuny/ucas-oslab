#include <os/sched.h>
#include <csr.h>
#include <logger.h>
#include <os/mm.h>

// NOTE: A/C-core
static ptr_t kernMemCurr = FREEMEM_KERNEL;
#define MAX_PAGE_NUM ((ALLMEM_KERNEL - FREEMEM_KERNEL) / PAGE_SIZE)
static ptr_t start_addr[MAX_PAGE_NUM] = {0}; // stores first page's base addr

typedef struct page {
    int ref_count;
} page_t;

static int page_id(ptr_t addr) {
    return (addr - FREEMEM_KERNEL) / PAGE_SIZE;
}

static ptr_t page_addr(int id) {
    return FREEMEM_KERNEL + id * PAGE_SIZE;
}

ptr_t allocPage(int numPage) {
    // align PAGE_SIZE
    int counter = 0;
    while (counter < MAX_PAGE_NUM) {
        ptr_t ret = ROUND(kernMemCurr, PAGE_SIZE);
        int id = page_id(ret);
        bool available = true;
        for (int i = 0; i < numPage; i++) {
            if (id + i >= MAX_PAGE_NUM || start_addr[id + i]) {
                available = false;
                break;
            }
        }
        if (available) {
            kernMemCurr += numPage * PAGE_SIZE;
            for (int i = 0; i < numPage; i++) {
                start_addr[id + i] = ret;
            }
            return ret;
        } else {
            kernMemCurr += PAGE_SIZE;
            if (kernMemCurr >= ALLMEM_KERNEL) {
                kernMemCurr = FREEMEM_KERNEL;
            }
        }
        counter++;
    }
    asserts(false, "allocPage: out of memory");
    return 0;
}

ptr_t new_pgdir() {
    ptr_t pgdir = allocPage(1);
    clear_pgdir(pgdir);
    return pgdir;
}

// NOTE: Only need for S-core to alloc 2MB large page
#ifdef S_CORE
static ptr_t largePageMemCurr = LARGE_PAGE_FREEMEM;
ptr_t allocLargePage(int numPage) {
    // align LARGE_PAGE_SIZE
    ptr_t ret = ROUND(largePageMemCurr, LARGE_PAGE_SIZE);
    largePageMemCurr = ret + numPage * LARGE_PAGE_SIZE;
    return ret;
}
#endif

void freePage(ptr_t baseAddr) {
    int id = page_id(baseAddr); 
    if (id < 0) {
        pretty_loge("try to free kernel page");
        return;
    }
    asserts(id >= 0 && id < MAX_PAGE_NUM, "freePage: invalid addr");
    asserts(start_addr[id], "freePage: double free detected");
    ptr_t entry = start_addr[id];
    int entry_id = page_id(start_addr[id]);
    asserts(entry_id <= id, "freePage: corrupted start_addr");
    pretty_logd("free page block at addr 0x%x", kva2pa(entry));
    for (int i = entry_id; start_addr[i] == entry; i = (i + 1) % MAX_PAGE_NUM) {
        pretty_logd("  free page #%d", i);
        start_addr[i] = 0;
    }
}

void* kmalloc(size_t size) {
    // TODO [P4-task1] (design you 'kmalloc' here if you need):
}

static inline uintptr_t add_page(uint64_t vpn, PTE* pgdir, uint64_t extra_attrs) {
    ptr_t new_page = allocPage(1);
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
uintptr_t alloc_page_helper(uintptr_t va, uintptr_t pgdir) {
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
    asserts(get_attribute(current_pgdir[vpn2], _PAGE_PRESENT), "uva2kva: vpn2 not present");
    current_pgdir = (PTE*)pa2kva(get_pa(current_pgdir[vpn2]));
    asserts(get_attribute(current_pgdir[vpn1], _PAGE_PRESENT), "uva2kva: vpn1 not present");
    current_pgdir = (PTE*)pa2kva(get_pa(current_pgdir[vpn1]));
    asserts(get_attribute(current_pgdir[vpn0], _PAGE_PRESENT), "uva2kva: vpn0 not present");
    uintptr_t page_base = pa2kva(get_pa(current_pgdir[vpn0]));
    return page_base + (uva & (PAGE_SIZE - 1));
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

void open_user_memory() {
    asm volatile("csrs sstatus, %0" : : "r"(SR_SUM));
}

void close_user_memory() {
    asm volatile("csrc sstatus, %0" : : "r"(SR_SUM));
}

static void free_pgdir(uintptr_t pgdir) {
    for (int i = 0; i < PTE_ENTRY_NUM; i++) {
        PTE pte = ((PTE*)pgdir)[i];
        if (get_attribute(pte, _PAGE_PRESENT)) {
            if (get_attribute(pte, _PAGE_READ | _PAGE_WRITE | _PAGE_EXEC)) {
                if (get_attribute(pte, _PAGE_USER)) {
                    freePage(pa2kva(get_pa(pte)));
                }
            } else {
                free_pgdir(pa2kva(get_pa(pte)));
            }
        }
    }
    freePage(pgdir);
}

void use_kernel_satp() {
    set_satp(SATP_MODE_SV39, 0, PGDIR_PA >> NORMAL_PAGE_SHIFT);
    local_flush_tlb_all();
}

// NOTE: this function is dangerous, make sure pcb is not running
void cleanup_vm(pcb_t* pcb) {
    free_pgdir(pcb->pgdir);
    freePage(pcb->kernel_stack_bottom);
}
