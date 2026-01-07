#include <assert.h>
#include <logger.h>
#include <os/fs.h>
#include <os/lock.h>
#include <os/string.h>
#include <type.h>

#define NINODE 32

inode_t inodes[NINODE];

void init_inodes() {
    for (int i = 0; i < NINODE; i++) {
        inode_clear(&inodes[i]);
    }
}

// NOTE: returns an unlocked inode
inode_t* inode_alloc(int type) {
    asserts(
        type == FS_TYPE_FILE || type == FS_TYPE_DIR || type == FS_TYPE_DEV, "invalid inode type");
    for (int i = 1; i < superblock.inode_count; i++) {
        block_t* block = block_open(INODE2BLOCK(i));
        diskinode_t* diskinode = (diskinode_t*)block->data + INODE2OFFSET(i);
        if (diskinode->type == 0) {
            pretty_logi("alloc inode #%d of type %d", i, type);
            superblock.used_inode++;
            memset(diskinode, 0, sizeof(diskinode_t));
            diskinode->type = type;
            diskinode->inode_num = i;
            memset(diskinode->direct, -1, sizeof(diskinode->direct));
            diskinode->indirect = -1;
            diskinode->double_indirect = -1;
            block_close(block);
            return inode_ref(i);
        }
        block_close(block);
    }
    err_halt("no free inode available on disk");
    return NULL;
}

// NOTE: returns an unlocked inode
inode_t* inode_ref(int inode_num) {
    inode_t* empty = 0;
    for (int i = 0; i < NINODE; i++) {
        if (inodes[i].type != 0 && inodes[i].inode_num == inode_num && inodes[i].ref_count > 0) {
            inodes[i].ref_count++;
            return &inodes[i];
        }
        if (inodes[i].type == 0 && empty == 0) {
            empty = &inodes[i];
        }
    }
    asserts(empty != 0, "no free inode cache available");

    empty->inode_num = inode_num;
    empty->ref_count = 1;
    empty->valid = 0;
    return empty;
}

void inode_deref(inode_t* inode) {
    asserts(inode->ref_count > 0, "inode_close with ref_count <= 0");

    if (inode->ref_count == 1 && inode->valid && inode->link_count == 0) {
        inode_open(inode);
        inode_delete(inode);
        inode_close(inode);
    }

    inode->ref_count--;

    if (inode->ref_count == 0) {
        inode_clear(inode);
    }
}

void inode_open(inode_t* inode) {
    mutex_acquire(&inode->lock);
    if (!inode->valid) {
        block_t* block = block_open(INODE2BLOCK(inode->inode_num));
        diskinode_t* diskinode = &((diskinode_t*)block->data)[INODE2OFFSET(inode->inode_num)];
        memcpy(inode, diskinode, sizeof(diskinode_t));
        block_close(block);
        inode->valid = 1;
        asserts(inode->type != 0, "loaded inode with type 0");
    }
}

void inode_unlock(inode_t* inode) {
    asserts(inode->lock.acquired, "unlock a free inode");
    mutex_release(&inode->lock);
}

void inode_close(inode_t* inode) {
    inode_unlock(inode);
    inode_deref(inode);
}

void inode_clear(inode_t* inode) {
    asserts(inode->ref_count == 0, "inode_clear with ref_count > 0");
    if (inode->valid) {
        block_t* block = block_open(INODE2BLOCK(inode->inode_num));
        diskinode_t* diskinode = &((diskinode_t*)block->data)[INODE2OFFSET(inode->inode_num)];
        memcpy(diskinode, inode, sizeof(diskinode_t));
        block_close(block);
        inode->valid = 0;
    }
    // reset inode
    memset(inode, 0, sizeof(inode_t));
    memset(inode->direct, -1, sizeof(inode->direct));
    inode->indirect = -1;
    inode->double_indirect = -1;
    mutex_init(&inode->lock);
}

void inode_sync(inode_t* inode) {
    asserts(inode->lock.acquired, "sync a free inode");
    block_t* block = block_open(INODE2BLOCK(inode->inode_num));
    diskinode_t* diskinode = &((diskinode_t*)block->data)[INODE2OFFSET(inode->inode_num)];
    memcpy(diskinode, inode, sizeof(diskinode_t));
    block_close(block);
}

int inode_mapblock(inode_t* inode, int block_in_file) {
    int block_num = -1;

    if (block_in_file < NUM_DIRECT_BLOCKS) {
        block_num = inode->direct[block_in_file];
        if (block_num < 0) {
            block_num = block_allocset(0);
            if (block_num < 0) return -1;
            inode->direct[block_in_file] = block_num;
            return block_num;
        }
        return block_num;
    }
    block_in_file -= NUM_DIRECT_BLOCKS;

    if (block_in_file < NUM_INDIRECT_BLOCKS) {
        if (inode->indirect < 0) {
            int indirect_block = block_allocset(0xff);
            if (indirect_block < 0) return -1;
            inode->indirect = indirect_block;
        }
        block_t* blk = block_open(inode->indirect);
        int* arr = (int*)blk->data;
        if (arr[block_in_file] < 0) {
            block_num = block_allocset(0);
            if (block_num >= 0) {
                arr[block_in_file] = block_num;
            }
        } else {
            block_num = arr[block_in_file];
        }
        block_close(blk);
        return block_num;
    }

    block_in_file -= NUM_INDIRECT_BLOCKS;

    asserts(block_in_file < NUM_DOUBLE_INDIRECT_BLOCKS, "block_in_file out of range");

    if (inode->double_indirect < 0) {
        int double_indirect_block = block_allocset(0xff);
        if (double_indirect_block < 0) return -1;
        inode->double_indirect = double_indirect_block;
    }

    block_t* root_blk = block_open(inode->double_indirect);
    int* root_arr = (int*)root_blk->data;

    int index = block_in_file / NUM_INDIRECT_BLOCKS;
    int offset = block_in_file % NUM_INDIRECT_BLOCKS;

    if (root_arr[index] < 0) {
        int direct = block_allocset(0xff);
        if (direct < 0) {
            block_close(root_blk);
            return -1;
        }
        root_arr[index] = direct;
    }

    block_t* blk = block_open(root_arr[index]);
    int* arr = (int*)blk->data;
    if (arr[offset] < 0) {
        block_num = block_allocset(0);
        if (block_num >= 0) {
            arr[offset] = block_num;
        }
    } else {
        block_num = arr[offset];
    }
    block_close(blk);

    block_close(root_blk);

    return block_num;
}

void inode_delete(inode_t* inode) {
    pretty_logd("deleting inode %d", inode->inode_num);

    for (int i = 0; i < NUM_DIRECT_BLOCKS; i++) {
        if (inode->direct[i] >= 0) {
            block_free(inode->direct[i]);
            inode->direct[i] = -1;
        }
    }

    if (inode->indirect >= 0) {
        block_t* blk = block_open(inode->indirect);
        int* arr = (int*)blk->data;
        for (int i = 0; i < NUM_INDIRECT_BLOCKS; i++) {
            if (arr[i] >= 0) {
                block_free(arr[i]);
            }
        }
        block_close(blk);
        block_free(inode->indirect);
        inode->indirect = -1;
    }

    if (inode->double_indirect >= 0) {
        block_t* root_blk = block_open(inode->double_indirect);
        int* root_arr = (int*)root_blk->data;
        for (int i = 0; i < NUM_INDIRECT_BLOCKS; i++) {
            if (root_arr[i] >= 0) {
                block_t* blk = block_open(root_arr[i]);
                int* arr = (int*)blk->data;
                for (int j = 0; j < NUM_INDIRECT_BLOCKS; j++) {
                    if (arr[j] >= 0) {
                        block_free(arr[j]);
                    }
                }
                block_close(blk);
                block_free(root_arr[i]);
            }
        }
        block_close(root_blk);
        block_free(inode->double_indirect);
        inode->double_indirect = -1;
    }

    inode->valid = 0;
    inode->type = 0;
    superblock.used_inode--;
}

int inode_read(inode_t* inode, void* dest, uint32_t pgdir, uint32_t offset, uint32_t length) {
    if (offset > inode->size || offset + length < offset) {
        pretty_logw(
            "read out of range: inode %d, offset %d, length %d, size %d", inode->inode_num, offset,
            length, inode->size);
        return 0;
    }
    pretty_logd("reading %d bytes from inode %d at offset %d", length, inode->inode_num, offset);
    if (offset + length > inode->size) {
        length = inode->size - offset;
        pretty_logd(
            "adjust read length to %d bytes due to end of file at %d bytes", length, inode->size);
    }

    uint32_t chunk_size = 0, total = 0;

    for (total = 0; total < length; total += chunk_size, dest += chunk_size, offset += chunk_size) {
        uint32_t block_in_file = offset / BLOCK_SIZE;
        uint32_t block_offset = offset % BLOCK_SIZE;
        pretty_logd("offset: %d, blockid: %d, offset: %d", offset, block_in_file, block_offset);
        int block_num = inode_mapblock(inode, block_in_file);
        if (block_num < 0) {
            pretty_logw("failed to map block %d of inode %d", block_in_file, inode->inode_num);
            break;
        }
        block_t* blk = block_open(block_num);
        chunk_size = min(BLOCK_SIZE - block_offset, length - total);
        if (pgdir) {
            memcpy_kva2uva((uva_t)dest, (kva_t)(blk->data + block_offset), chunk_size, pgdir);
        } else {
            memcpy(dest, blk->data + block_offset, chunk_size);
        }
        block_close(blk);
        pretty_logd(
            "read %d bytes from inode %d at offset %d", chunk_size, inode->inode_num, offset);
    }
    pretty_logd("total: %d bytes", total);

    return total;
}

int inode_write(inode_t* inode, void* src, uint32_t pgdir, uint32_t offset, uint32_t length) {
    pretty_logd("writing %d bytes to inode %d at offset %d", length, inode->inode_num, offset);
    uint32_t chunk_size = 0, total = 0;

    for (total = 0; total < length; total += chunk_size, src += chunk_size) {
        uint32_t block_in_file = offset / BLOCK_SIZE;
        uint32_t block_offset = offset % BLOCK_SIZE;
        int block_num = inode_mapblock(inode, block_in_file);
        if (block_num < 0) {
            break;
        }
        block_t* blk = block_open(block_num);
        chunk_size = min(BLOCK_SIZE - block_offset, length - total);
        if (pgdir) {
            memcpy_uva2kva((kva_t)(blk->data + block_offset), (uva_t)src, chunk_size, pgdir);
        } else {
            memcpy(blk->data + block_offset, src, chunk_size);
        }
        block_close(blk);
        offset += chunk_size;
    }

    if (offset > inode->size) {
        inode->size = offset;
    }

    inode_sync(inode);

    return total;
}
