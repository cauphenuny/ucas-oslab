#include <assert.h>
#include <logger.h>
#include <os/fs.h>
#include <os/lock.h>
#include <os/mm.h>
#include <os/string.h>

#define DENTRY_CACHE_BUCKETS 256

typedef struct dcache_entry {
    struct dcache_entry* next;
    uint32_t parent_inode;
    size_t offset;
    dentry_t dentry;
} dcache_entry_t;

static dcache_entry_t** dcache_table;
static spin_lock_t dcache_lock;
static bool dcache_enabled = true;

static uint32_t dcache_hash(uint32_t parent_inode, const char* name) {
    uint32_t hash = 2166136261u ^ parent_inode;
    int i = 0;
    for (; i < MAX_FILE_NAME && name[i]; i++) {
        hash ^= (uint32_t)(unsigned char)name[i];
        hash *= 16777619u;
    }
    hash ^= (uint32_t)i;
    hash *= 16777619u;
    return hash & (DENTRY_CACHE_BUCKETS - 1);
}

static void dcache_free_entry_list(dcache_entry_t* entry) {
    while (entry) {
        dcache_entry_t* next = entry->next;
        kfree(entry);
        entry = next;
    }
}

void init_dentry_cache() {
    if (!dcache_table) {
        dcache_table = kmalloc(sizeof(dcache_entry_t*) * DENTRY_CACHE_BUCKETS);
        asserts(dcache_table != NULL, "dentry cache allocation failed");
        spin_lock_init(&dcache_lock);
        memset(dcache_table, 0, sizeof(dcache_entry_t*) * DENTRY_CACHE_BUCKETS);
    } else {
        dcache_reset();
    }
    spin_lock_acquire(&dcache_lock);
    dcache_enabled = true;
    spin_lock_release(&dcache_lock);
}

void dcache_reset() {
    if (!dcache_table) return;
    spin_lock_acquire(&dcache_lock);
    for (int i = 0; i < DENTRY_CACHE_BUCKETS; i++) {
        dcache_free_entry_list(dcache_table[i]);
        dcache_table[i] = NULL;
    }
    spin_lock_release(&dcache_lock);
}

bool dcache_get(uint32_t parent_inode, const char* name, dentry_t* out, size_t* offset) {
    if (!dcache_table || !name) return false;
    uint32_t bucket = dcache_hash(parent_inode, name);
    spin_lock_acquire(&dcache_lock);
    if (!dcache_enabled) {
        spin_lock_release(&dcache_lock);
        return false;
    }
    for (dcache_entry_t* entry = dcache_table[bucket]; entry; entry = entry->next) {
        if (entry->parent_inode == parent_inode &&
            strncmp(entry->dentry.name, name, MAX_FILE_NAME) == 0) {
            if (out) memcpy(out, &entry->dentry, sizeof(dentry_t));
            if (offset) *offset = entry->offset;
            spin_lock_release(&dcache_lock);
            return true;
        }
    }
    spin_lock_release(&dcache_lock);
    return false;
}

void dcache_put(uint32_t parent_inode, const dentry_t* entry, size_t offset) {
    if (!dcache_table || !entry || entry->inode_num == 0) return;
    uint32_t bucket = dcache_hash(parent_inode, entry->name);
    spin_lock_acquire(&dcache_lock);
    if (!dcache_enabled) {
        spin_lock_release(&dcache_lock);
        return;
    }
    for (dcache_entry_t* node = dcache_table[bucket]; node; node = node->next) {
        if (node->parent_inode == parent_inode &&
            strncmp(node->dentry.name, entry->name, MAX_FILE_NAME) == 0) {
            memcpy(&node->dentry, entry, sizeof(dentry_t));
            node->offset = offset;
            spin_lock_release(&dcache_lock);
            return;
        }
    }
    dcache_entry_t* node = kmalloc(sizeof(dcache_entry_t));
    if (!node) {
        pretty_logw("dentry cache allocation failed, dropping entry");
        spin_lock_release(&dcache_lock);
        return;
    }
    node->parent_inode = parent_inode;
    node->offset = offset;
    memcpy(&node->dentry, entry, sizeof(dentry_t));
    node->next = dcache_table[bucket];
    dcache_table[bucket] = node;
    spin_lock_release(&dcache_lock);
}

void dcache_remove(uint32_t parent_inode, const char* name) {
    if (!dcache_table || !name) return;
    uint32_t bucket = dcache_hash(parent_inode, name);
    spin_lock_acquire(&dcache_lock);
    if (!dcache_enabled) {
        spin_lock_release(&dcache_lock);
        return;
    }
    dcache_entry_t** cursor = &dcache_table[bucket];
    while (*cursor) {
        dcache_entry_t* entry = *cursor;
        if (entry->parent_inode == parent_inode &&
            strncmp(entry->dentry.name, name, MAX_FILE_NAME) == 0) {
            *cursor = entry->next;
            kfree(entry);
            break;
        }
        cursor = &(*cursor)->next;
    }
    spin_lock_release(&dcache_lock);
}

void dcache_invalidate(uint32_t parent_inode) {
    if (!dcache_table) return;
    spin_lock_acquire(&dcache_lock);
    if (!dcache_enabled) {
        spin_lock_release(&dcache_lock);
        return;
    }
    for (int i = 0; i < DENTRY_CACHE_BUCKETS; i++) {
        dcache_entry_t** cursor = &dcache_table[i];
        while (*cursor) {
            dcache_entry_t* entry = *cursor;
            if (entry->parent_inode == parent_inode) {
                *cursor = entry->next;
                kfree(entry);
                continue;
            }
            cursor = &(*cursor)->next;
        }
    }
    spin_lock_release(&dcache_lock);
}

void dcache_set_enabled(bool enabled) {
    if (!dcache_table) {
        dcache_enabled = enabled;
        if (enabled) {
            init_dentry_cache();
        }
        return;
    }

    spin_lock_acquire(&dcache_lock);
    bool changed = (dcache_enabled != enabled);
    dcache_enabled = enabled;
    spin_lock_release(&dcache_lock);

    if (!enabled || changed) {
        dcache_reset();
    }
}

bool dcache_is_enabled() {
    if (!dcache_table) {
        return dcache_enabled;
    }
    spin_lock_acquire(&dcache_lock);
    bool enabled = dcache_enabled;
    spin_lock_release(&dcache_lock);
    return enabled;
}
