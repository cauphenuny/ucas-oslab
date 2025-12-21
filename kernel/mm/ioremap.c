#include <logger.h>
#include <os/ioremap.h>
#include <os/mm.h>
#include <pgtable.h>
#include <type.h>

// maybe you can map it to IO_ADDR_START ?
static kva_t io_base = IO_ADDR_START;

void* ioremap(unsigned long phys_addr, unsigned long size) {
    // DONE: [p5-task1] map one specific physical region to virtual address
    uintptr_t page_size = PAGE_SIZE, scale = 1 << PPN_BITS;
    int num_pgdirs = 3;
    while (page_size * scale < size) {
        page_size *= scale;
        num_pgdirs--;
    }
    uintptr_t aligned_addr = ROUNDDOWN(phys_addr, page_size);
    uintptr_t aligned_size = ROUND(phys_addr + size - aligned_addr, page_size);
    pretty_logi(
        "bind %lx(=%lx)[+%lx] to %lx, num_pgdirs: %d, num_pages: %d", phys_addr, aligned_addr, size,
        io_base, num_pgdirs, aligned_size / page_size);

    kva_t addr = io_base;
    io_base += aligned_size;

    for (unsigned i = 0; i < aligned_size; i += page_size) {
        PTE* pte = find_kernel_pte(addr + i, true, num_pgdirs);
        asserts(pte, "invalid pagetable entry");
        asserts(!get_attribute(*pte, _PAGE_PRESENT), "mapping existed");
        bind_addr(pte, phys_addr + i, _PAGE_READ | _PAGE_WRITE | _PAGE_DIRTY | _PAGE_ACCESSED);
    }
    return (void*)addr;
}

void iounmap(void* io_addr) {
    // TODO: [p5-task1] a very naive iounmap() is OK
    // maybe no one would call this function?
}
