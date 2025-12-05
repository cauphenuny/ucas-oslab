#include <logger.h>
#include <os/list.h>
#include <os/lock.h>
#include <os/mm.h>
#include <os/sched.h>
#include <os/string.h>
#include <pgtable.h>

#define PIPE_MAX_COUNT    32
#define PIPE_NAME_MAX_LEN 32
#define PIPE_PAGE_SIZE    PAGE_SIZE
#define PIPE_PAGE_ATTRS   (_PAGE_USER | _PAGE_READ | _PAGE_WRITE | _PAGE_EXEC)

typedef enum {
    PIPE_SEG_PHYS,
    PIPE_SEG_SWAP,
} pipe_segment_type_t;

typedef struct pipe_segment {
    list_node_t node;
    pipe_segment_type_t type;
    kva_t page;
    uint64_t swap_id;
    size_t length;
} pipe_segment_t;

typedef struct pipe_entry {
    bool used;
    char name[PIPE_NAME_MAX_LEN];
    spin_lock_t lock;
    list_t segments;
    list_t reader_wait_list;
    size_t buffered_bytes;
    int refcnt;
    pid_bitmap_t ref_bitmap;
    bool closing;
} pipe_entry_t;

static bool pipe_initialized;
static spin_lock_t pipe_table_lock;
static pipe_entry_t pipe_table[PIPE_MAX_COUNT];

static void pipe_entry_clear(pipe_entry_t* pipe);
static pipe_segment_t* pipe_detach_page_from_sender(uva_t va, pageframe_group_t* owner_group);
static void pipe_free_segment(pipe_segment_t* seg);
static void pipe_destroy_locked(pipe_entry_t* pipe);

static inline bool pipe_name_equals(const char* lhs, const char* rhs) {
    return strncmp(lhs, rhs, PIPE_NAME_MAX_LEN) == 0;
}

static void pipe_entry_clear(pipe_entry_t* pipe) {
    list_init(&pipe->segments, "pipe_segments");
    list_init(&pipe->reader_wait_list, "pipe_reader_wait");
    pipe->buffered_bytes = 0;
    pipe->refcnt = 0;
    pipe->ref_bitmap = 0;
    pipe->closing = false;
    pipe->used = false;
    pipe->name[0] = '\0';
}

static void pipe_free_segment(pipe_segment_t* seg) {
    if (!seg) {
        return;
    }
    if (seg->type == PIPE_SEG_PHYS) {
        attach_pageframe(seg->page, PAGE_GROUP_KERNEL);
        free_pageframe(seg->page);
    } else {
        free_swap(seg->swap_id);
    }
    kfree(seg);
}

static void pipe_release_segments_locked(pipe_entry_t* pipe) {
    while (true) {
        list_node_t* node = list_shift(&pipe->segments);
        if (!node) {
            break;
        }
        pipe_segment_t* seg = container_of(node, pipe_segment_t, node);
        if (pipe->buffered_bytes >= seg->length) {
            pipe->buffered_bytes -= seg->length;
        } else {
            pipe->buffered_bytes = 0;
        }
        pipe_free_segment(seg);
    }
}

static void pipe_destroy_locked(pipe_entry_t* pipe) {
    spin_lock_acquire(&pipe->lock);
    pipe->closing = true;
    pipe_release_segments_locked(pipe);
    spin_lock_release(&pipe->lock);

    unblock_list(&pipe->reader_wait_list, pipe->name);

    spin_lock_acquire(&pipe->lock);
    pipe_entry_clear(pipe);
    spin_lock_release(&pipe->lock);
}

static void pipe_system_init(void) {
    if (pipe_initialized) {
        return;
    }
    pipe_initialized = true;
    spin_lock_init(&pipe_table_lock);
    for (int i = 0; i < PIPE_MAX_COUNT; i++) {
        pipe_entry_t* pipe = &pipe_table[i];
        spin_lock_init(&pipe->lock);
        pipe_entry_clear(pipe);
    }
}

static pipe_entry_t* pipe_lookup_by_index(int idx) {
    if (idx < 0 || idx >= PIPE_MAX_COUNT) {
        return NULL;
    }
    if (!pipe_table[idx].used) {
        return NULL;
    }
    return &pipe_table[idx];
}

static pipe_segment_t* pipe_alloc_segment(void) {
    pipe_segment_t* seg = (pipe_segment_t*)kmalloc(sizeof(pipe_segment_t));
    if (seg) {
        memset(seg, 0, sizeof(pipe_segment_t));
        seg->length = PIPE_PAGE_SIZE;
    }
    return seg;
}

static pipe_segment_t* pipe_detach_page_from_sender(uva_t va, pageframe_group_t* owner_group) {
    PTE* pte = find_pte(va, current_running->pgdir, false);
    if (!pte) {
        pretty_loge("invalid VA 0x%lx", va);
        return NULL;
    }
    if (!(get_attribute(*pte, _PAGE_PRESENT) || get_attribute(*pte, _PAGE_SOFT))) {
        pretty_loge("VA 0x%lx not mapped", va);
        return NULL;
    }

    pipe_segment_t* seg = pipe_alloc_segment();
    if (!seg) {
        pretty_loge("kmalloc pipe_segment failed");
        return NULL;
    }

    if (get_attribute(*pte, _PAGE_PRESENT)) {
        kva_t page = pa2kva(get_pa(*pte));
        pageframe_t* attr = get_page_attr(page);
        attr->pte = NULL;
        attr->last_accessed = 0;
        detach_pageframe(page, owner_group);
        *pte = 0;
        seg->type = PIPE_SEG_PHYS;
        seg->page = page;
    } else {
        seg->type = PIPE_SEG_SWAP;
        seg->swap_id = get_pfn(*pte);
        *pte = 0;
    }
    local_flush_tlb_page(va);
    return seg;
}

static void pipe_clear_dest_mapping(PTE* pte) {
    if (!pte) {
        return;
    }
    if (get_attribute(*pte, _PAGE_PRESENT)) {
        kva_t old_page = pa2kva(get_pa(*pte));
        pageframe_t* attr = get_page_attr(old_page);
        attr->pte = NULL;
        attr->last_accessed = 0;
        free_pageframe(old_page);
    } else if (get_attribute(*pte, _PAGE_SOFT)) {
        uint64_t swap_id = get_pfn(*pte);
        free_swap(swap_id);
    }
    *pte = 0;
}

static int pipe_install_segment_to_dest(
    pipe_segment_t* seg, PTE* dest_pte, uva_t dest_va, pageframe_group_t* dest_group) {
    if (seg->type == PIPE_SEG_PHYS) {
        attach_pageframe(seg->page, dest_group);
        bind_page(dest_pte, seg->page, PIPE_PAGE_ATTRS);
    } else {
        set_attribute(dest_pte, _PAGE_SOFT);
        set_pfn(dest_pte, seg->swap_id);
    }
    local_flush_tlb_page(dest_va);
    return 0;
}

static pipe_segment_t* pipe_pop_segment(pipe_entry_t* pipe) {
    while (1) {
        spin_lock_acquire(&pipe->lock);
        list_node_t* node = list_shift(&pipe->segments);
        if (node) {
            pipe_segment_t* seg = container_of(node, pipe_segment_t, node);
            if (pipe->buffered_bytes >= seg->length) {
                pipe->buffered_bytes -= seg->length;
            } else {
                pipe->buffered_bytes = 0;
            }
            spin_lock_release(&pipe->lock);
            return seg;
        }
        if (pipe->closing || !pipe->used) {
            spin_lock_release(&pipe->lock);
            return NULL;
        }
        do_block(&current_running->sched_node, &pipe->reader_wait_list);
        spin_lock_release(&pipe->lock);
        do_scheduler();
    }
}

static bool pipe_push_segment(pipe_entry_t* pipe, pipe_segment_t* seg) {
    bool pushed = false;
    spin_lock_acquire(&pipe->lock);
    if (!pipe->closing && pipe->used) {
        list_append(&pipe->segments, &seg->node);
        pipe->buffered_bytes += seg->length;
        pushed = true;
    }
    spin_lock_release(&pipe->lock);
    if (pushed) {
        unblock_list(&pipe->reader_wait_list, pipe->name);
    }
    return pushed;
}

// NOTE: the address must be aligned to PAGE_SIZE
int pipe_open(const char* name) {
    pipe_system_init();
    if (!name) {
        return -1;
    }

    char local_name[PIPE_NAME_MAX_LEN];
    strncpy(local_name, name, PIPE_NAME_MAX_LEN - 1);
    local_name[PIPE_NAME_MAX_LEN - 1] = '\0';
    int pcb_index = get_pcb_index(current_running->pid);
    pid_bitmap_t pid_bit = pcb_index >= 0 ? (1ull << pcb_index) : 0;

    spin_lock_acquire(&pipe_table_lock);
    int free_idx = -1;
    for (int i = 0; i < PIPE_MAX_COUNT; i++) {
        pipe_entry_t* pipe = &pipe_table[i];
        if (pipe->used && pipe_name_equals(pipe->name, local_name)) {
            if (pid_bit && !(pipe->ref_bitmap & pid_bit)) {
                pipe->ref_bitmap |= pid_bit;
                pipe->refcnt++;
            }
            spin_lock_release(&pipe_table_lock);
            return i;
        }
        if (!pipe->used && free_idx == -1) {
            free_idx = i;
        }
    }

    if (free_idx == -1) {
        spin_lock_release(&pipe_table_lock);
        pretty_loge("no free slot for %s", local_name);
        return -1;
    }

    pipe_entry_t* target = &pipe_table[free_idx];
    pipe_entry_clear(target);
    strncpy(target->name, local_name, PIPE_NAME_MAX_LEN - 1);
    target->name[PIPE_NAME_MAX_LEN - 1] = '\0';
    target->used = true;
    target->closing = false;
    if (pid_bit) {
        target->ref_bitmap |= pid_bit;
        target->refcnt = 1;
    }
    spin_lock_init(&target->lock);

    spin_lock_release(&pipe_table_lock);
    return free_idx;
}

// send length bytes data from src to pipe[idx]
long pipe_give_pages(int idx, kva_t src, size_t length) {
    pipe_system_init();
    pipe_entry_t* pipe = pipe_lookup_by_index(idx);
    if (!pipe) {
        pretty_loge("invalid pipe idx %d", idx);
        return -1;
    }
    if ((src & (PIPE_PAGE_SIZE - 1)) || (length & (PIPE_PAGE_SIZE - 1))) {
        pretty_loge("unaligned src=0x%lx len=%lu", src, length);
        return -1;
    }
    if (length == 0) {
        return 0;
    }

    size_t processed = 0;
    uva_t cursor = (uva_t)src;
    pageframe_group_t* owner_group = find_pagegroup(current_running->pgdir);

    while (processed < length) {
        pipe_segment_t* seg = pipe_detach_page_from_sender(cursor, owner_group);
        if (!seg) {
            break;
        }
        if (!pipe_push_segment(pipe, seg)) {
            pipe_free_segment(seg);
            break;
        }
        processed += PIPE_PAGE_SIZE;
        cursor += PIPE_PAGE_SIZE;
    }

    if (processed == 0) {
        return -1;
    }
    return processed;
}

// receive length(aligned) bytes data from pipe[idx] to dest
long pipe_take_pages(int idx, kva_t dest, size_t length) {
    pipe_system_init();
    pipe_entry_t* pipe = pipe_lookup_by_index(idx);
    if (!pipe) {
        pretty_loge("invalid pipe idx %d", idx);
        return -1;
    }
    if ((dest & (PIPE_PAGE_SIZE - 1)) || (length & (PIPE_PAGE_SIZE - 1))) {
        pretty_loge("unaligned dest=0x%lx len=%lu", dest, length);
        return -1;
    }
    if (length == 0) {
        return 0;
    }

    size_t processed = 0;
    uva_t cursor = (uva_t)dest;
    pageframe_group_t* dest_group = find_pagegroup(current_running->pgdir);

    while (processed < length) {
        PTE* dest_pte = find_pte(cursor, current_running->pgdir, true);
        if (!dest_pte) {
            break;
        }
        pipe_segment_t* seg = pipe_pop_segment(pipe);
        if (!seg) {
            break;
        }
        pipe_clear_dest_mapping(dest_pte);
        pipe_install_segment_to_dest(seg, dest_pte, cursor, dest_group);
        kfree(seg);
        processed += PIPE_PAGE_SIZE;
        cursor += PIPE_PAGE_SIZE;
    }

    if (processed == 0) {
        return -1;
    }
    return processed;
}

void init_pipe(void) { pipe_system_init(); }

void cleanup_pipe(pid_t pid) {
    pipe_system_init();
    int pcb_index = get_pcb_index(pid);
    if (pcb_index < 0) {
        return;
    }
    pid_bitmap_t mask = 1ull << pcb_index;
    spin_lock_acquire(&pipe_table_lock);
    for (int i = 0; i < PIPE_MAX_COUNT; i++) {
        pipe_entry_t* pipe = &pipe_table[i];
        if (!pipe->used) {
            continue;
        }
        if (!(pipe->ref_bitmap & mask)) {
            continue;
        }
        pipe->ref_bitmap &= ~mask;
        if (pipe->refcnt > 0) {
            pipe->refcnt--;
        }
        if (pipe->refcnt == 0) {
            pipe_destroy_locked(pipe);
        }
    }
    spin_lock_release(&pipe_table_lock);
}
