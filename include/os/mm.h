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

extern kva_t alloc_pageframe(int num_page);
void free_pageframe(kva_t base_addr);

extern kva_t new_pgdir();

extern void* kmalloc(size_t size);
extern void kfree(void* ptr);
extern void init_kmalloc(void);

extern void share_pgtable(kva_t dest_pgdir, kva_t src_pgdir);
extern kva_t alloc_page_va(kva_t va, kva_t pgdir);

// TODO [P4-task4]: shm_page_get/dt */
kva_t shm_page_get(int key);
void shm_page_dt(kva_t addr);

// NOTE: assume use 3-level page table
kva_t uva2kva(uva_t uva, kva_t pgdir);

void memcpy_kva2uva(kva_t dest_va, kva_t src, size_t size, kva_t pgdir_dest);
void strcpy_kva2uva(kva_t dest_va, const char* src, kva_t pgdir_dest);

void cleanup_vm(pcb_t* pcb);

void init_vm();

typedef struct pageframe_group {
    list_t pages;
    size_t capacity;
    size_t used;
    int refcount;  // one group may be shared by multiple processes
} pageframe_group_t;

extern pageframe_group_t page_groups[NUM_MAX_TASK];

pageframe_group_t* find_pageframe_group(uintptr_t pgdir);

// swap out one page from group, return its addr(in kva)
kva_t swapout(pageframe_group_t* group);

typedef struct pageframe {
    uint64_t last_accessed;
    list_node_t group_node;
} pageframe_t;

#define MAX_PAGE_NUM ((ALLMEM_KERNEL - FREEMEM_KERNEL) / PAGE_SIZE)

extern pageframe_t pages[MAX_PAGE_NUM];

extern int swap_location;

#endif /* MM_H */
