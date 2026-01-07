#include <os/fs.h>
#include <os/kernel.h>
#include <pgtable.h>

#define BLOCK_CONCURRENCY 8

static block_t blocks[BLOCK_CONCURRENCY];

void show_blocks() {
    for (int i = 0; i < BLOCK_CONCURRENCY; i++) {
        if (blocks[i].valid) {
            printk(
                "block buffer %d: block_num=%d, refcnt=%d\n", i, blocks[i].block_num,
                blocks[i].refcnt);
        }
    }
}

void block_read(int block_num, void* data) {
    uint32_t src = superblock.start_sector + block_num * NSECTOR_BLOCK;
    cached_block_read(data, src);
}

void block_write(int block_num, void* data) {
    uint32_t dest = superblock.start_sector + block_num * NSECTOR_BLOCK;
    cached_block_write(data, dest);
}

block_t* block_open(int block_num) {
    for (int i = 0; i < BLOCK_CONCURRENCY; i++) {
        if (blocks[i].valid && blocks[i].block_num == block_num) {
            blocks[i].refcnt++;
            return &blocks[i];
        }
    }
    for (int i = 0; i < BLOCK_CONCURRENCY; i++) {
        if (!blocks[i].valid || blocks[i].refcnt == 0) {
            if (blocks[i].valid) {
                block_write(blocks[i].block_num, blocks[i].data);
            }
            block_t* blk = &blocks[i];
            block_read(block_num, blk->data);
            blk->valid = 1;
            blk->block_num = block_num;
            blk->refcnt = 1;
            return blk;
        }
    }
    err_halt("no available block buffer");
    return NULL;
}

void block_close(block_t* blk) {
    asserts(blk->refcnt > 0, "block_close with refcnt <= 0");
    blk->refcnt--;
}

void block_memset(int block_num, uint8_t val) {
    block_t* blk = block_open(block_num);
    memset(blk->data, val, BLOCK_SIZE);
    block_close(blk);
}

// returns allocated block number
int block_alloc() {
    for (int bblock = 0; bblock < superblock.block_count; bblock += BLOCK_SIZE * 8) {
        block_t* map = block_open(BLOCKID2MAPBLOCK(bblock));
        for (int boffset = 0;
             boffset < BLOCK_SIZE * 8 && (bblock + boffset) < superblock.block_count; boffset++) {
            int byte_offset = boffset / 8;
            int bit_in_byte = boffset % 8;
            int mask = 1 << bit_in_byte;
            if (!(map->data[byte_offset] & mask)) {
                // found free block
                map->data[byte_offset] |= mask;
                block_close(map);
                superblock.used_block++;
                pretty_logn("allocated block %d", bblock + boffset);
                return superblock.datablock_offset + bblock + boffset;
            }
        }
        block_close(map);
    }
    return -1;
}

int block_allocset(uint8_t val) {
    int block_num = block_alloc();
    if (block_num >= 0) {
        block_memset(block_num, val);
    }
    return block_num;
}

void block_free(int block_num) {
    asserts(block_num >= superblock.datablock_offset, "block_free on non-data block");
    block_num -= superblock.datablock_offset;
    block_t* map = block_open(BLOCKID2MAPBLOCK(block_num));
    int bit_offset = BLOCKID2MAPOFFSET(block_num);
    int byte_offset = bit_offset / 8;
    int bit_in_byte = bit_offset % 8;
    int mask = 1 << bit_in_byte;
    asserts(map->data[byte_offset] & mask, "block_free on free block");
    map->data[byte_offset] &= ~mask;
    block_close(map);
    superblock.used_block--;
    pretty_logn("freed block %d", block_num);
}

void flush_block_cache() {
    for (int i = 0; i < BLOCK_CONCURRENCY; i++) {
        if (blocks[i].valid) {
            if (blocks[i].refcnt != 0) {
                pretty_logw(
                    "block %d closed with refcnt %d", blocks[i].block_num, blocks[i].refcnt);
            }
            pretty_logn("writeback block #%d", blocks[i].block_num);
            block_write(blocks[i].block_num, blocks[i].data);
        }
    }
}
