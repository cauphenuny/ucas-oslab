#include <logger.h>
#include <os/kernel.h>
#include <os/mm.h>
#include <os/task.h>

int swap_base_location;

#define SWAP_SIZE    (64 * 1024 * 1024)  // 64MB
#define NUM_MAX_SWAP (SWAP_SIZE / PAGE_SIZE)

#define SWAP_LEN (PAGE_SIZE / SECTOR_SIZE)

int8_t swap_using[SWAP_SIZE / PAGE_SIZE] = {0};
uint64_t swap_used, swap_next_idx;

uint64_t swap_counter_in, swap_counter_out;

static uint64_t alloc_swap() {
    if (swap_used >= NUM_MAX_SWAP) {
        pretty_loge("out of swap space!");
        asserts(false, "alloc_swap: out of swap space");
    }
    while (swap_using[swap_next_idx]) {
        swap_next_idx = (swap_next_idx + 1) % NUM_MAX_SWAP;
    }
    swap_using[swap_next_idx] = 1;
    swap_used++;
    return swap_next_idx;
}

void free_swap(uint64_t swap_id) {
    swap_using[swap_id] = 0;
    swap_used--;
}

// NOTE: do not swapout pagedir in grouop, only swapout leaf nodes
kva_t swapout(pageframe_group_t* group) {
    list_foreach_node_reversed(iter, &group->pages.head) {
        pageframe_t* pf = container_of(iter, pageframe_t, group_node);
        PTE* pte = pf->pte;
        if (pte && get_attribute(*pte, _PAGE_EXEC | _PAGE_READ | _PAGE_WRITE)) {
            // found a leaf page
            kva_t page = pageframe_addr(pf - pages);
            // find a swap location
            uint64_t swap_id = alloc_swap();
            pretty_logn("swapping out page 0x%x to swap id 0x%x", kva2pa(page), swap_id);
            bios_sd_write(page, SWAP_LEN, swap_id * SWAP_LEN + swap_base_location);
            clear_attribute(pte, _PAGE_PRESENT);
            set_attribute(pte, _PAGE_SOFT);
            set_pfn(pte, swap_id);
            pageframe_destruct(pf, page, group);
            swap_counter_out++;
            return page;
        }
    }

    pretty_loge("no leaf page to swap out in group '%s', killing related proc...", group->pages.name);
    bool exit = false;
    for (int i = 0; i < NUM_MAX_PCB; i++) {
        pcb_t* pcb = pcb_all[i];
        if (pcb->status == TASK_EXITED) continue;
        if (find_pagegroup(pcb->pgdir) == group) {
            pretty_logi("kill proc %d '%s'", pcb->pid, pcb->name);
            if (pcb->pid == current_running->pid) exit = true;
            else do_kill(pcb->pid);
        }
    }
    if (exit) do_exit();
    return 0;
}

void swapin(uva_t uva, kva_t pgdir, kva_t page) {
    PTE* pte = (PTE*)pgdir;
    uint64_t vpn2, vpn1, vpn0;
    get_vpn(uva, &vpn2, &vpn1, &vpn0);
    PTE entry_level2 = pte[vpn2];
    asserts(get_attribute(entry_level2, _PAGE_PRESENT), "swapin: level 2 entry not present");
    kva_t level1_pgdir = pa2kva(get_pa(entry_level2));
    PTE entry_level1 = ((PTE*)level1_pgdir)[vpn1];
    asserts(get_attribute(entry_level1, _PAGE_PRESENT), "swapin: level 1 entry not present");
    kva_t level0_pgdir = pa2kva(get_pa(entry_level1));
    PTE* entry_level0 = &((PTE*)level0_pgdir)[vpn0];
    asserts(get_attribute(*entry_level0, _PAGE_SOFT), "swapin: level 0 entry not swapped out");
    // find swap location
    uint64_t swap_id = get_pfn(*entry_level0);
    pretty_logn("swapping in page 0x%x from swap id 0x%x", kva2pa(page), swap_id);
    bios_sd_read(page, SWAP_LEN, swap_id * SWAP_LEN + swap_base_location);
    clear_attribute(entry_level0, _PAGE_SOFT);
    bind_page(entry_level0, page, _PAGE_USER | _PAGE_READ | _PAGE_WRITE | _PAGE_EXEC);
    free_swap(swap_id);
    swap_counter_in++;
}

void show_swap() {
    printk("swap: capacity=%lu, used=%lu\n", NUM_MAX_SWAP, swap_used);
    printk("counter: swap_in=%lu, swap_out=%lu\n", swap_counter_in, swap_counter_out);
}
