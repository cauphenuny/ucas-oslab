/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *            Copyright (C) 2018 Institute of Computing Technology, CAS
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *                                   Memory Management
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * */
#ifndef MM_H
#define MM_H

#include <os/sched.h>
#include <os/smp.h>
#include <pgtable.h>
#include <type.h>

#define NUM_MAX_PAGEGROUP 64

#define MAP_KERNEL        1
#define MAP_USER          2
#define MEM_SIZE          32
#define PAGE_SIZE         4096  // 4K
#define PTE_ENTRY_NUM     (PAGE_SIZE / sizeof(PTE))
#define INIT_KERNEL_STACK 0xffffffc052000000
#define FREEMEM_KERNEL    (INIT_KERNEL_STACK + PAGE_SIZE * NR_CPUS)
#define ALLMEM_KERNEL     0xffffffc060000000

/* Rounding; only works for n = power of two */
#define ROUND(a, n)     (((((uint64_t)(a)) + (n) - 1)) & ~((n) - 1))
#define ROUNDDOWN(a, n) (((uint64_t)(a)) & ~((n) - 1))

extern size_t get_free_memory();

// #define S_CORE
// NOTE: only need for S-core to alloc 2MB large page
#ifdef S_CORE
#define LARGE_PAGE_FREEMEM 0xffffffc056000000
#define USER_STACK_ADDR    0x400000
extern ptr_t allocLargePage(int numPage);
#else
// NOTE: A/C-core
#define USER_STACK_ADDR 0xf00010000
#endif

kva_t bind_page(PTE* pte, kva_t page, uint64_t extra_attrs);

extern void* kmalloc(size_t size);
extern void kfree(void* ptr);
extern void init_kmalloc(void);

extern void share_pgtable(kva_t dest_pgdir, kva_t src_pgdir);
extern PTE* alloc_page_va(uva_t va, kva_t pgdir);
extern PTE* bind_page_va(uva_t va, kva_t pgdir, kva_t page);

extern PTE* find_pte(uva_t va, kva_t pgdir, bool create);

// NOTE: the address must be aligned to PAGE_SIZE
extern int pipe_open(const char* name);
// send length bytes data from src to pipe[idx]
extern long pipe_give_pages(int idx, kva_t src, size_t length);
// receive length(aligned) bytes data from pipe[idx] to dest
extern long pipe_take_pages(int idx, kva_t dest, size_t length);
extern void init_pipe();
extern void cleanup_pipe(pid_t pid);

// NOTE: assume use 3-level page table
extern kva_t uva2kva(uva_t uva, kva_t pgdir);

extern void memcpy_kva2uva(kva_t dest_va, kva_t src, size_t size, kva_t pgdir_dest);
extern void strcpy_kva2uva(kva_t dest_va, const char* src, kva_t pgdir_dest);

extern void cleanup_vm(pcb_t* pcb);

struct pageframe;
struct pageframe_group;

// NOTE: optional: init, cleanup, on_timer
struct pagegroup_vtable {
    void (*init)(struct pageframe_group* group);     // set algo for this group (init algo data)
    void (*cleanup)(struct pageframe_group* group);  // unset algo (cleanup algo data)
    void (*attach)(struct pageframe_group* group, int pageframe_id);  // attach pageframe to group
    void (*detach)(struct pageframe_group* group, int pageframe_id);  // detach pageframe from group

    void (*on_timer)(struct pageframe_group* group, uint64_t current_tick);
    void (*on_access)(struct pageframe_group* group, kva_t addr, uint64_t current_tick);
    void (*on_write)(struct pageframe_group* group, kva_t addr, uint64_t current_tick);

    list_node_t* (*evict)(struct pageframe_group* group);  // select a page to swapout
    void (*show)(struct pageframe_group* group, struct pageframe* pf);
};

typedef const struct pagegroup_vtable pagegroup_vtable_t;

extern pagegroup_vtable_t *const PAGEGROUP_VTABLE_LRU, *const PAGEGROUP_VTABLE_FIFO;

typedef struct pageframe_group {
    list_t pages;
    size_t capacity;
    size_t used;
    int refcount;                // one group may be shared by multiple processes
    pagegroup_vtable_t* vtable;  // page replacement algorithm & other algorithm-specific func
} pageframe_group_t;

extern pageframe_group_t page_groups[NUM_MAX_PAGEGROUP];
extern pageframe_group_t* const PAGE_GROUP_KERNEL;

extern pageframe_group_t* find_pagegroup(kva_t page);
static inline pageframe_group_t* get_current_pagegroup() {
    return find_pagegroup(current_running->pgdir);
}
static inline int pagegroup_getid(pageframe_group_t* group) { return group - page_groups; }

// NOTE: fork a new pageframe group and move all memory under pgdir to it
extern int
fork_pagegroup(kva_t top_pgdir, size_t capacity, const char* name);  // success: 0, otherwise 1
extern int resize_pagegroup(pageframe_group_t* group, size_t new_capacity);
extern void shrink_pagegroup(pageframe_group_t* group, size_t space);
extern void free_pagegroup(pageframe_group_t* group);

extern void show_pagegroups(int argc, char** argv);

extern kva_t new_top_pgdir(pageframe_group_t* group);

// swap out one page from group, return its addr(in kva)
extern kva_t swapout(pageframe_group_t* group);

// swap in one page to given page, then bind it to pgdir
// NOTE: page must be disattached from any pgdir when passes to this function
extern void swapin(uva_t uva, kva_t pgdir, kva_t page);

extern void show_swap();
extern void free_swap(uint64_t swap_id);

typedef struct pageframe {
    list_node_t group_node;
    PTE* pte;
} pageframe_t;

#define MAX_PAGE_NUM ((ALLMEM_KERNEL - FREEMEM_KERNEL) / PAGE_SIZE)

extern void pageframe_destruct(pageframe_t* pf, kva_t addr);

extern pageframe_t pages[MAX_PAGE_NUM];
static inline int pageframe_kva2id(ptr_t addr) { return (addr - FREEMEM_KERNEL) / PAGE_SIZE; }
static inline int pageframe_attr2id(pageframe_t* pf) { return pf - pages; }
static inline ptr_t pageframe_id2addr(int id) { return FREEMEM_KERNEL + id * PAGE_SIZE; }
static inline ptr_t pageframe_attr2kva(pageframe_t* pf) {
    return pageframe_id2addr(pageframe_attr2id(pf));
}
extern pageframe_t* pageframe_kva2attr(kva_t page);

extern kva_t alloc_pageframe(pageframe_group_t* group, int num_page);
extern void free_pageframe(kva_t base_addr);
static inline void attach_pageframe(kva_t addr, pageframe_group_t* group) {
    group->vtable->attach(group, pageframe_kva2id(addr));
}
static inline void detach_pageframe(kva_t addr, pageframe_group_t* group) {
    group->vtable->detach(group, pageframe_kva2id(addr));
}

extern int swap_base_location;

extern void init_vm();
extern void init_pagegroup();

#endif /* MM_H */
