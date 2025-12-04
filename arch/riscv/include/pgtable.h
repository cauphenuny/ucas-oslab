#ifndef PGTABLE_H
#define PGTABLE_H

#include <os/string.h>
#include <type.h>
#include <csr.h>
#include <assert.h>

#define SATP_MODE_SV39 8
#define SATP_MODE_SV48 9

#define SATP_ASID_SHIFT 44lu
#define SATP_MODE_SHIFT 60lu

#define NORMAL_PAGE_SHIFT 12lu
#define NORMAL_PAGE_SIZE (1lu << NORMAL_PAGE_SHIFT)
#define LARGE_PAGE_SHIFT 21lu
#define LARGE_PAGE_SIZE (1lu << LARGE_PAGE_SHIFT)

/*
 * Flush entire local TLB.  'sfence.vma' implicitly fences with the instruction
 * cache as well, so a 'fence.i' is not necessary.
 */
static inline void local_flush_tlb_all(void)
{
    __asm__ __volatile__ ("sfence.vma" : : : "memory");
}

/* Flush one page from local TLB */
static inline void local_flush_tlb_page(unsigned long addr)
{
    __asm__ __volatile__ ("sfence.vma %0" : : "r" (addr) : "memory");
}

static inline void local_flush_icache_all(void)
{
    asm volatile ("fence.i" ::: "memory");
}

static inline void set_satp(
    unsigned mode, unsigned asid, unsigned long ppn)
{
    unsigned long __v =
        (unsigned long)(((unsigned long)mode << SATP_MODE_SHIFT) | ((unsigned long)asid << SATP_ASID_SHIFT) | ppn);
    __asm__ __volatile__("sfence.vma\ncsrw satp, %0" : : "rK"(__v) : "memory");
}

static inline void open_user_memory() {
    asm volatile("csrs sstatus, %0" : : "r"(SR_SUM));
}

static inline void close_user_memory() {
    asm volatile("csrc sstatus, %0" : : "r"(SR_SUM));
}

#define PGDIR_PA 0x51000000lu  // use 51000000 page as PGDIR

static inline void use_kernel_satp() {
    set_satp(SATP_MODE_SV39, 0, PGDIR_PA >> NORMAL_PAGE_SHIFT);
    local_flush_tlb_all();
}


/*
 * PTE format:
 * | XLEN-1  10 | 9             8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0
 *       PFN      reserved for SW   D   A   G   U   X   W   R   V
 */

#define _PAGE_ACCESSED_OFFSET 6

#define _PAGE_PRESENT (1 << 0)
#define _PAGE_READ (1 << 1)     /* Readable */
#define _PAGE_WRITE (1 << 2)    /* Writable */
#define _PAGE_EXEC (1 << 3)     /* Executable */
#define _PAGE_USER (1 << 4)     /* User */
#define _PAGE_GLOBAL (1 << 5)   /* Global */
#define _PAGE_ACCESSED (1 << 6) /* Set by hardware on any access \
                                 */
#define _PAGE_DIRTY (1 << 7)    /* Set by hardware on any write */
#define _PAGE_SOFT (1 << 8)     /* Reserved for software */

#define _PAGE_PFN_SHIFT 10lu

#define VA_MASK ((1lu << 39) - 1)

#define PPN_BITS 9lu
#define NUM_PTE_ENTRY (1 << PPN_BITS)

#define VPN_MASK ((1lu << PPN_BITS) - 1)

typedef uint64_t PTE;

typedef uintptr_t kva_t;
typedef uintptr_t uva_t;
typedef uintptr_t pa_t;

#define KERNEL_ADDR_TAG 0xffffffc000000000lu
#define PGDIR_VA (PGDIR_PA | KERNEL_ADDR_TAG)

#define VA_EFFECTIVE_MASK ((1lu << 38) - 1)

#define PA_TOP 0x60000000lu
#define PA_BOTTOM 0x50000000lu

/* Translation between physical addr and kernel virtual addr */
static inline pa_t kva2pa(kva_t kva)
{
    /* TODO: [P4-task1] */
    asserts((kva & (~VA_EFFECTIVE_MASK)) == KERNEL_ADDR_TAG, "kva2pa called with invalid kva");
    return kva ^ KERNEL_ADDR_TAG;
}

static inline kva_t pa2kva(pa_t pa)
{
    /* TODO: [P4-task1] */
    asserts(pa >= PA_BOTTOM && pa < PA_TOP, "pa2kva called with invalid pa");
    return pa | KERNEL_ADDR_TAG;
}

/* get physical page addr from PTE 'entry' */
static inline pa_t get_pa(PTE entry)
{
    /* TODO: [P4-task1] */
    return (entry >> _PAGE_PFN_SHIFT) << NORMAL_PAGE_SHIFT;
}

/* Get/Set page frame number of the `entry` */
static inline long get_pfn(PTE entry)
{
    /* TODO: [P4-task1] */
    return entry >> _PAGE_PFN_SHIFT;
}
static inline void set_pfn(PTE *entry, uint64_t pfn)
{
    /* TODO: [P4-task1] */
    *entry = (*entry & ((1lu << _PAGE_PFN_SHIFT) - 1)) | (pfn << _PAGE_PFN_SHIFT);
}

/* Get/Set attribute(s) of the `entry` */
static inline long get_attribute(PTE entry, uint64_t mask)
{
    /* TODO: [P4-task1] */
    return entry & mask;
}

static inline void set_attribute(PTE *entry, uint64_t bits)
{
    /* TODO: [P4-task1] */
    asserts((bits & (~((1 << _PAGE_PFN_SHIFT) - 1))) == 0, "set_attribute with invalid bits");
    *entry = (*entry) | bits;
}

static inline void clear_attribute(PTE *entry, uint64_t bits)
{
    *entry = (*entry) & (~bits);
}

static inline void clear_pgdir(kva_t pgdir_addr)
{
    /* TODO: [P4-task1] */
    memset((void*)pgdir_addr, 0, NORMAL_PAGE_SIZE);
}

static inline void get_vpn(uva_t va, uint64_t* vpn2, uint64_t* vpn1, uint64_t* vpn0) {
    va &= VA_MASK;
    *vpn2 = (va >> (NORMAL_PAGE_SHIFT + PPN_BITS + PPN_BITS)) & VPN_MASK;
    *vpn1 = (va >> (NORMAL_PAGE_SHIFT + PPN_BITS)) & VPN_MASK;
    *vpn0 = (va >> NORMAL_PAGE_SHIFT) & VPN_MASK;
}


#endif  // PGTABLE_H
