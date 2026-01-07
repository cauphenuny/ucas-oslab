#include <logger.h>
#include <os/fs.h>
#include <os/kernel.h>
#include <os/mm.h>

uintptr_t cache_pgdir;
pageframe_group_t* fs_cache_group;
#define FS_CACHE_SIZE (8 * 1024)  // 8K pages, 32M

cache_config_t pagecache_config;

uva_t cache_swapin(int start_sector) {
    uva_t va = (uva_t)start_sector * SECTOR_SIZE;
    pretty_logd("calculated virtual address 0x%x for sector %d", va, start_sector);
    PTE* pte = find_pte(va, cache_pgdir, true);
    if (*pte != 0) {
        if (!get_attribute(*pte, _PAGE_PRESENT)) {
            asserts(get_attribute(*pte, _PAGE_SOFT), "invalid pte state");
            kva_t page = alloc_pageframe(fs_cache_group, BLOCK_SIZE / PAGE_SIZE);
            swapin(va, cache_pgdir, page);
        }
    } else {
        kva_t page = alloc_pageframe(fs_cache_group, BLOCK_SIZE / PAGE_SIZE);
        pageframe_t* attr = pageframe_kva2attr(page);
        attr->uva = va;
        bios_sd_read(page, NSECTOR_BLOCK, start_sector);
        bind_page(pte, page, _PAGE_USER | _PAGE_READ | _PAGE_WRITE);
    }
    return va;
}

void cached_block_read(void* dest, int start_sector) {
    uva_t uva = cache_swapin(start_sector);
    memcpy_uva2kva((kva_t)dest, uva, BLOCK_SIZE, cache_pgdir);
}

void cached_block_write(void* src, int start_sector) {
    uva_t uva = cache_swapin(start_sector);
    memcpy_kva2uva(uva, (kva_t)src, BLOCK_SIZE, cache_pgdir);
}

uint64_t fs_cache_swap_alloc(pageframe_group_t* group, uva_t va) {
    asserts(va % BLOCK_SIZE == 0, "va not block aligned");
    return va / SECTOR_SIZE;
}

void fs_cache_swap_free(pageframe_group_t* group, uint64_t uva) {
    static uint8_t buffer[BLOCK_SIZE];
    uint64_t sector_id = uva / SECTOR_SIZE;
    if (sector_id < FS_START_SECTOR) return;
    memcpy_uva2kva((kva_t)buffer, uva, BLOCK_SIZE, cache_pgdir);
    pretty_logn("fs cache writing back block to SD card, sector %d", sector_id);
    bios_sd_write((kva_t)buffer, NSECTOR_BLOCK, sector_id);
}

pagegroup_vtable_t fs_swap_vtable;

kva_t create_fs_pgdir() {
    kva_t pgdir = new_top_pgdir(get_current_pagegroup());
    pretty_logd("allcoated pgdir 0x%x for fs cache", kva2pa(pgdir));
    fork_pagegroup(pgdir, FS_CACHE_SIZE, "pagecache");
    fs_cache_group = find_pagegroup(pgdir);
    fs_swap_vtable.swap_alloc = fs_cache_swap_alloc;
    fs_swap_vtable.on_page_free = fs_cache_swap_free;
    fs_cache_group->vtable = &fs_swap_vtable;
    return pgdir;
}

void cache_routine() {
    while (true) {
        do_sleep(max(1, pagecache_config.write_back_freq));
        if (pagecache_config.policy == POLICY_WRITE_BACK) {
            pretty_logi("fs cache write-back routine triggered");
            flush_filesystem();
        }
    }
}

void init_fs_cache() {
    memcpy(&fs_swap_vtable, get_current_pagegroup()->vtable, sizeof(pagegroup_vtable_t));
    cache_pgdir = create_fs_pgdir();
    pagecache_config.write_back_freq = 10;  // s
    do_exec(
        NULL, "cache_daemon", (uint64_t)cache_routine, 1, (char*[]){"cache_daemon"}, (unsigned)-1);
}

void flush_fs_cache() {
    free_top_pgdir(cache_pgdir);
    cache_pgdir = create_fs_pgdir();
}
