extern "C" {
#include <logger.h>
#include <os/fs.h>
#include <os/kernel.h>
#include <os/string.h>
#include <os/task.h>
}

static fdesc_t fdesc_array[NUM_FDESCS];
superblock_t init_fs();
static superblock_t superblock = init_fs();
static uint8_t buffer[SECTOR_SIZE];
static uint8_t block_buffer[SECTOR_SIZE * SIZE_BLOCK];

inode_t get_inode(int inode_num) {
    asserts(inode_num >= 0 && inode_num < superblock.inode_count, "invalid inode number");
    int sector = inode_num / INODE_PER_SECTOR, offset = inode_num % INODE_PER_SECTOR;
    bios_sd_read((kva_t)buffer, 1, superblock.start_sector + superblock.inode_offset + sector);
    inode_t* inodes = (inode_t*)buffer;
    return inodes[offset];
}

void write_inode(int inode_num, inode_t* inode) {
    asserts(inode_num >= 0 && inode_num < superblock.inode_count, "invalid inode number");
    int sector = inode_num / INODE_PER_SECTOR, offset = inode_num % INODE_PER_SECTOR;
    bios_sd_read((kva_t)buffer, 1, superblock.start_sector + superblock.inode_offset + sector);
    inode_t* inodes = (inode_t*)buffer;
    inodes[offset] = *inode;
    bios_sd_write((kva_t)buffer, 1, superblock.start_sector + superblock.inode_offset + sector);
}

void read_block(int block_num, void* data) {
    asserts(block_num >= 0 && block_num < superblock.block_count, "invalid block number");
    bios_sd_read(
        (kva_t)data, SIZE_BLOCK,
        superblock.start_sector + superblock.datablock_offset + block_num * SIZE_BLOCK);
}

void write_block(int block_num, void* data) {
    asserts(block_num >= 0 && block_num < superblock.block_count, "invalid block number");
    bios_sd_write(
        (kva_t)data, SIZE_BLOCK,
        superblock.start_sector + superblock.datablock_offset + block_num * SIZE_BLOCK);
}

template <size_t num_sectors> struct sector_cache_t {
    int buffer_sid{-1};  // sector id
    uint8_t buffer[SECTOR_SIZE * num_sectors];
    void read(int sector_id) {
        if (buffer_sid == sector_id) return;
        if (buffer_sid >= 0) write();
        buffer_sid = sector_id;
        bios_sd_read((kva_t)buffer, num_sectors, sector_id);
    }
    void write() { bios_sd_write((kva_t)buffer, num_sectors, buffer_sid); }
    ~sector_cache_t() {
        if (buffer_sid >= 0) write();
    }
};

class bitmap_proxy_t {
    sector_cache_t<1> sector;
    int start_sector;
    int size;
    uint32_t& counter;
    struct bitmap_entry_proxy_t {
        bitmap_proxy_t* host;
        int index;
        operator bool() {
            int offset_byte = index % 8;
            int offset_sector = (index / 8) % SECTOR_SIZE;
            int sector_id = (index / 8) / SECTOR_SIZE;
            host->sector.read(host->start_sector + sector_id);
            uint8_t byte = host->sector.buffer[offset_sector];
            return (byte & (1 << offset_byte)) != 0;
        };
        bitmap_entry_proxy_t& operator=(bool value) {
            int offset_byte = index % 8;
            int offset_sector = (index / 8) % SECTOR_SIZE;
            int sector_id = (index / 8) / SECTOR_SIZE;
            host->sector.read(host->start_sector + sector_id);
            uint8_t& byte = host->sector.buffer[offset_sector];
            if (value) {
                byte |= (1 << offset_byte);
            } else {
                byte &= ~(1 << offset_byte);
            }
            return *this;
        }
    };

public:
    bitmap_proxy_t(int start_sector, int size, uint32_t& counter)
        : start_sector(start_sector), size(size), counter(counter) {
        counter = 0;
        for (int i = 0; i < size; i++) {
            if ((*this)[i]) {
                counter++;
            }
        }
        pretty_logd("initialized bitmap, start_sector=%d, size=%d, used=%d", start_sector, size, counter);
    }
    bitmap_entry_proxy_t operator[](int index) { return bitmap_entry_proxy_t{this, index}; }
    int alloc() {
        for (int i = 0; i < size; i++) {
            if (!(*this)[i]) {
                (*this)[i] = true;
                counter++;
                return i;
            }
        }
        return -1;
    }
    void free(int id) {
        asserts(id >= 0 && id < size, "bitmap_proxy_t::free with invalid id");
        (*this)[id] = false;
        counter--;
    }
};

struct filesystem_t {
    superblock_t& meta;
    bitmap_proxy_t block_map;
    bitmap_proxy_t inode_map;

    int sync() {
        if (bios_sd_write((kva_t)&meta, 1, meta.start_sector)) {
            pretty_loge("failed to write superblock");
            return 1;
        }
        return 0;
    }

    filesystem_t(superblock_t& meta)
        : meta(meta),
          block_map(meta.start_sector + meta.block_map_offset, meta.block_count, meta.used_block),
          inode_map(meta.start_sector + meta.inode_map_offset, meta.inode_count, meta.used_inode) {
        pretty_logd("initialized filesystem, used inodes: %d, used blocks: %d", meta.used_inode, meta.used_block);
    }

    ~filesystem_t() {
        pretty_logd("syncing filesystem metadata to disk...");
        sync();
    }
} fs(superblock);

class file_t {
    int inode_num;
    inode_t inode;
    uint32_t find_block(int file_block_id) {
        asserts(file_block_id >= 0, "find_block with negative file_block_id");
        asserts(file_block_id < inode.blocks, "file_block_id out of range");
        if (file_block_id < NUM_DIRECT_BLOCKS) {
            return inode.direct[file_block_id];
        } else if (file_block_id < INODE_PER_BLOCK + NUM_DIRECT_BLOCKS) {
            // single indirect
            read_block(inode.indirect, block_buffer);
            uint32_t* indirect_block = (uint32_t*)block_buffer;
            return indirect_block[file_block_id - NUM_DIRECT_BLOCKS];
        } else {
            // double indirect
            read_block(inode.double_inderect, block_buffer);
            int first_level_index =
                (file_block_id - NUM_DIRECT_BLOCKS - INODE_PER_BLOCK) / INODE_PER_BLOCK;
            int second_level_index =
                (file_block_id - NUM_DIRECT_BLOCKS - INODE_PER_BLOCK) % INODE_PER_BLOCK;
            uint32_t* double_indirect_block = (uint32_t*)block_buffer;
            read_block(double_indirect_block[first_level_index], block_buffer);
            uint32_t* indirect_block = (uint32_t*)block_buffer;
            return indirect_block[second_level_index];
        }
    }
    struct block_range_t {
        file_t* file;
        struct block_iterator_t {
            file_t* file;
            int block_id;  // index in file data blocks
            uint32_t operator*() { return file->find_block(block_id); }
            block_iterator_t& operator++() {
                inode_t inode = get_inode(file->inode_num);
                int total_blocks = (inode.size + SIZE_BLOCK - 1) / SIZE_BLOCK;  // ceil division
                block_id++;
                if (block_id >= total_blocks) {
                    block_id = -1;  // end()
                }
                return *this;
            }
            block_iterator_t operator++(int) {
                block_iterator_t temp = *this;
                ++(*this);
                return temp;
            }
        };
        block_iterator_t begin() { return block_iterator_t{file, 0}; }
        block_iterator_t end() { return block_iterator_t{file, -1}; }
    };

public:
    auto blocks() { return block_range_t{this}; }
    void append(uint32_t block_num) {
        if (inode.blocks < NUM_DIRECT_BLOCKS) {
            inode.direct[inode.blocks] = block_num;
        } else if (inode.blocks == NUM_DIRECT_BLOCKS) {
            // allocate single indirect block
            memset(block_buffer, 0, SIZE_BLOCK);
            inode.indirect = superblock.used_block;
            superblock.used_block++;
            write_block(inode.indirect, block_buffer);
            inode.direct[inode.blocks] = block_num;
        } else if (inode.blocks < NUM_DIRECT_BLOCKS + INODE_PER_BLOCK) {
            inode.direct[inode.blocks] = block_num;
        } else if (inode.blocks == NUM_DIRECT_BLOCKS + INODE_PER_BLOCK) {
            // allocate double indirect block
            memset(block_buffer, 0, SIZE_BLOCK);
            inode.double_inderect = superblock.used_block;
            superblock.used_block++;
            write_block(inode.double_inderect, block_buffer);
            // allocate first level indirect block
            memset(block_buffer, 0, SIZE_BLOCK);
            uint32_t* double_indirect_block = (uint32_t*)block_buffer;
            double_indirect_block[0] = superblock.used_block;
            superblock.used_block++;
            write_block(inode.double_inderect, block_buffer);
            inode.direct[inode.blocks] = block_num;
        } else {
            // allocate more indirect blocks as needed
            int index = (inode.blocks - NUM_DIRECT_BLOCKS - INODE_PER_BLOCK) / INODE_PER_BLOCK;
            int offset = (inode.blocks - NUM_DIRECT_BLOCKS - INODE_PER_BLOCK) % INODE_PER_BLOCK;
            if (offset == 0) {
                // need to allocate a new first level indirect block
                memset(block_buffer, 0, SIZE_BLOCK);
                uint32_t* double_indirect_block = (uint32_t*)block_buffer;
                double_indirect_block[index] = superblock.used_block;
                superblock.used_block++;
                write_block(inode.double_inderect, block_buffer);
                // initialize the new first level indirect block
                memset(block_buffer, 0, SIZE_BLOCK);
                write_block(double_indirect_block[index], block_buffer);
            }
            // now we can add the data block
            read_block(
                ((uint32_t*)block_buffer)[index], block_buffer);  // read first level indirect
            uint32_t* indirect_block = (uint32_t*)block_buffer;
        }
    }
};

void attach_dir(directory_t* dest, const char* name, int inode) {
    dentry_t* dest_entry = dest->entries + dest->num_entries;
    strcpy(dest_entry->name, name);
    dest_entry->inode_num = inode;
    dest->num_entries++;
}

void mkdir(directory_t* dest, int parent_inode, const char* name, int inode) {
    memset(dest, 0, sizeof(directory_t));
    attach_dir(dest, ".", inode);
    attach_dir(dest, "..", parent_inode);
}

int chdir(directory_t* parent, const char* child) {
    for (int i = 0; i < parent->num_entries; i++) {
        dentry_t* entry = &parent->entries[i];
        if (strcmp(entry->name, child) == 0) {
            return entry->inode_num;
        }
    }
    return -1;  // not found
}

int do_mkfs(void) {
    // TODO [P6-task1]: Implement do_mkfs
    uint32_t block_map = 1;  // superblock is at sector 0
    uint32_t inode_map = block_map + SIZE_BLOCK_MAP;
    uint32_t inode_table = inode_map + SIZE_INODE_MAP;
    uint32_t data = inode_table + SIZE_INODE_TABLE;
    uint32_t fs_size = FS_END_SECTOR - FS_START_SECTOR;

    static superblock_t sb;
    sb = (superblock_t){
        .magic = SUPERBLOCK_MAGIC,
        .fs_size = FS_END_SECTOR - FS_START_SECTOR,
        .start_sector = FS_START_SECTOR,
        .root_inode = ROOT_INODE,
        .block_map_offset = block_map,
        .inode_map_offset = inode_map,
        .inode_offset = inode_table,
        .datablock_offset = data,
        .inode_count = NUM_INODES,
        .block_count = (fs_size - data) / SIZE_BLOCK,
        .used_inode = 1,  // root inode
        .used_block = 0,
    };
    pretty_logd("created superblock, fs_size: %d, start_sector: %d", sb.fs_size, sb.start_sector);
    pretty_logd(
        "block_map_offset: %d, inode_map_offset: %d", sb.block_map_offset, sb.inode_map_offset);
    pretty_logd("inode_offset: %d, data_offset: %d", sb.inode_offset, sb.datablock_offset);
    pretty_logd("inode_count: %d, block_count: %d", sb.inode_count, sb.block_count);

    if (bios_sd_write((kva_t)&sb, 1, FS_START_SECTOR)) {
        pretty_logw("failed to write sb to SD card");
        return 1;
    }
    memset(buffer, 0, SECTOR_SIZE);
    buffer[0] |= 1;
    if (bios_sd_write((kva_t)buffer, 1, sb.start_sector + sb.inode_map_offset)) {
        pretty_logw("failed to write inode bitmap to SD card");
        return 1;
    }
    return 0;  // do_mkfs succeeds
}

superblock_t init_fs() {
    superblock_t sb;
    if (bios_sd_read((kva_t)&sb, 1, FS_START_SECTOR)) {
        pretty_loge("failed to read superblock from sd-card");
    }
    if (sb.magic != SUPERBLOCK_MAGIC) {
        pretty_logw(
            "invalid filesystem magic number: expected 0x%x, got 0x%x", SUPERBLOCK_MAGIC, sb.magic);
        do_mkfs();
        bios_sd_read((kva_t)&sb, 1, FS_START_SECTOR);
    }
    pretty_log(
        LOG_INFO, "filesystem initialized: size=%d sectors, inode_count=%d, block_count=%d",
        sb.fs_size, sb.inode_count, sb.block_count);
    return sb;
}

int do_statfs(void) {
    // TODO [P6-task1]: Implement do_statfs
    if (bios_sd_read((kva_t)&superblock, 1, FS_START_SECTOR)) {
        pretty_logw("failed to read superblock from SD card");
        return 1;
    }
    printk("Filesystem Infomation:\n");
    if (superblock.magic != SUPERBLOCK_MAGIC) {
        printk("<uninitialized>");
        return 1;
    }
    printk("Total size: %d sectors at 0x%x\n", superblock.fs_size, superblock.start_sector);
    printk("Block map: %d sectors at #%d\n", SIZE_BLOCK_MAP, superblock.block_map_offset);
    printk("Inode map: %d sectors at #%d\n", SIZE_INODE_MAP, superblock.inode_map_offset);
    printk(
        "Inode table: %d entries, %d sectors at #%d, used: %d(%d%%)\n", NUM_INODES,
        SIZE_INODE_TABLE, superblock.inode_offset, superblock.used_inode,
        superblock.used_inode * 100 / superblock.inode_count);
    printk(
        "Data blocks: %d entries at #%d, used: %d(%d%%)\n", superblock.block_count,
        superblock.datablock_offset, superblock.used_block,
        superblock.used_block * 100 / superblock.block_count);
    return 0;  // do_statfs succeeds
}

int do_cd(char* path) {
    return 0;  // do_cd succeeds
}

int do_mkdir(char* path) {
    // TODO [P6-task1]: Implement do_mkdir

    return 0;  // do_mkdir succeeds
}

int do_rmdir(char* path) {
    // TODO [P6-task1]: Implement do_rmdir

    return 0;  // do_rmdir succeeds
}

int do_ls(char* path, int option) {
    // TODO [P6-task1]: Implement do_ls
    // Note: argument 'option' serves for 'ls -l' in A-core

    return 0;  // do_ls succeeds
}

int do_open(char* path, int mode) {
    // TODO [P6-task2]: Implement do_open

    return 0;  // return the id of file descriptor
}

int do_read(int fd, char* buff, int length) {
    // TODO [P6-task2]: Implement do_read

    return 0;  // return the length of trully read data
}

int do_write(int fd, char* buff, int length) {
    // TODO [P6-task2]: Implement do_write

    return 0;  // return the length of trully written data
}

int do_close(int fd) {
    // TODO [P6-task2]: Implement do_close

    return 0;  // do_close succeeds
}

int do_ln(char* src_path, char* dst_path) {
    // TODO [P6-task2]: Implement do_ln

    return 0;  // do_ln succeeds
}

int do_rm(char* path) {
    // TODO [P6-task2]: Implement do_rm

    return 0;  // do_rm succeeds
}

int do_lseek(int fd, int offset, int whence) {
    // TODO [P6-task2]: Implement do_lseek

    return 0;  // the resulting offset location from the beginning of the file
}
