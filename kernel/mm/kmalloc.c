#include <assert.h>
#include <logger.h>
#include <os/mm.h>
#include <os/string.h>

#define POOL_PAGES     1024  // allocate 1024 pages (4 MiB) for kmalloc pool
#define MIN_ORDER      4    // 16 bytes minimum block
#define MIN_BLOCK_SIZE (1UL << MIN_ORDER)

#define POOL_BYTES ((POOL_PAGES) * (PAGE_SIZE))

#if ((POOL_BYTES & (POOL_BYTES - 1)) != 0)
#error "POOL_BYTES must be power of two"
#endif

/* compute log2(POOL_BYTES) at compile time */
#define ILOG2_CONST(x) (__builtin_ctzll(x))

#define MAX_ORDER (ILOG2_CONST(POOL_BYTES))

#define ORDER_COUNT (MAX_ORDER - MIN_ORDER + 1)

typedef struct free_block {
    struct free_block* next;
} free_block_t;

typedef struct {
    uint32_t order;
    uint32_t magic;
} block_header_t;

static free_block_t* free_lists[ORDER_COUNT];
static kva_t pool_base;
static kva_t pool_end;

#define HEADER_MAGIC 0xC0DECAFEu

static inline size_t order_size(int order) { return 1UL << order; }
static inline int order_index(int order) { return order - MIN_ORDER; }

static void push_block(int order, kva_t addr) {
    int idx = order_index(order);
    free_block_t* block = (free_block_t*)addr;
    block->next = free_lists[idx];
    free_lists[idx] = block;
}

static kva_t pop_block(int order) {
    int idx = order_index(order);
    free_block_t* block = free_lists[idx];
    if (!block) return 0;
    free_lists[idx] = block->next;
    return (kva_t)block;
}

static kva_t acquire_block(int order) {
    for (int cur = order; cur <= MAX_ORDER; cur++) {
        kva_t block = pop_block(cur);
        if (!block) continue;
        while (cur > order) {
            cur--;
            kva_t buddy = block + order_size(cur);
            push_block(cur, buddy);
        }
        return block;
    }
    return 0;
}

static void merge_block(kva_t addr, int order) {
    while (order < MAX_ORDER) {
        kva_t offset = addr - pool_base;
        kva_t buddy = pool_base + (offset ^ order_size(order));
        int idx = order_index(order);
        free_block_t** prev = &free_lists[idx];
        free_block_t* cur = *prev;
        bool found = false;
        while (cur) {
            if ((kva_t)cur == buddy) {
                *prev = cur->next;
                found = true;
                break;
            }
            prev = &cur->next;
            cur = cur->next;
        }
        if (!found) break;
        if (buddy < addr) addr = buddy;
        order++;
    }
    push_block(order, addr);
}

void init_kmalloc() {
    pool_base = alloc_pageframe(POOL_PAGES);
    asserts(pool_base != 0, "init_kmalloc: alloc_pageframe failed");
    pool_end = pool_base + POOL_BYTES;
    memset((void*)free_lists, 0, sizeof(free_lists));
    push_block(MAX_ORDER, pool_base);
    pretty_log(LOG_INFO, "kmalloc pool initialized: base=0x%lx size=%lu", pool_base, POOL_BYTES);
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;
    size_t total = size + sizeof(block_header_t);
    if (total < MIN_BLOCK_SIZE) total = MIN_BLOCK_SIZE;
    int order = MIN_ORDER;
    while (order <= MAX_ORDER && order_size(order) < total) order++;
    if (order > MAX_ORDER) {
        pretty_log(LOG_ERROR, "kmalloc: request too large (%lu bytes)", size);
        return NULL;
    }
    kva_t block = acquire_block(order);
    if (!block) {
        pretty_log(LOG_ERROR, "kmalloc: out of memory for %lu bytes", size);
        return NULL;
    }
    block_header_t* header = (block_header_t*)block;
    header->order = (uint32_t)order;
    header->magic = HEADER_MAGIC;
    return (void*)(block + sizeof(block_header_t));
}

void kfree(void* ptr) {
    if (!ptr) return;
    kva_t addr = (kva_t)ptr - sizeof(block_header_t);
    if (addr < pool_base || addr >= pool_end) {
        pretty_log(LOG_ERROR, "kfree: pointer 0x%lx out of range", (kva_t)ptr);
        return;
    }
    block_header_t* header = (block_header_t*)addr;
    if (header->magic != HEADER_MAGIC || header->order > MAX_ORDER || header->order < MIN_ORDER) {
        pretty_log(LOG_ERROR, "kfree: invalid block header at 0x%lx", addr);
        return;
    }
    header->magic = 0;
    merge_block(addr, (int)header->order);
}
