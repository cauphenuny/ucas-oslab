#include <tui.hpp>

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
static constexpr size_t PATH_BUF_SIZE = 256;

inode_t get_inode(int inode_num) {
    int sector = inode_num / INODE_PER_SECTOR, offset = inode_num % INODE_PER_SECTOR;
    bios_sd_read((kva_t)buffer, 1, superblock.start_sector + superblock.inode_offset + sector);
    inode_t* inodes = (inode_t*)buffer;
    inode_t ret = inodes[offset];
    pretty_logd("inode_num=%d, inode=0x%lx", inode_num, ret);
    return ret;
}

void write_inode(int inode_num, inode_t* inode) {
    pretty_logd("inode_num=%d, inode=0x%lx", inode_num, *inode);
    int sector = inode_num / INODE_PER_SECTOR, offset = inode_num % INODE_PER_SECTOR;
    pretty_logd("sector=%d, offset=%d", sector, offset);
    bios_sd_read((kva_t)buffer, 1, superblock.start_sector + superblock.inode_offset + sector);
    inode_t* inodes = (inode_t*)buffer;
    inodes[offset] = *inode;
    bios_sd_write((kva_t)buffer, 1, superblock.start_sector + superblock.inode_offset + sector);
}

void read_block(int block_num, void* data) {
    bios_sd_read(
        (kva_t)data, SIZE_BLOCK,
        superblock.start_sector + superblock.datablock_offset + block_num * SIZE_BLOCK);
}

void write_block(int block_num, void* data) {
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
        pretty_logd(
            "initialized bitmap, start_sector=%d, size=%d, used=%d", start_sector, size, counter);
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
        inode_t root_inode = get_inode(ROOT_INODE);
        pretty_logd(
            "initialized filesystem, used inodes: %d, used blocks: %d, root inode: %lx", meta.used_inode,
            meta.used_block, root_inode);
    }

    ~filesystem_t() {
        pretty_logd("syncing filesystem metadata to disk...");
        sync();
    }
} fs(superblock);

class file_t {
    int inode_num;
    inode_t inode;
    int pos;

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
            int first_level_index =
                (file_block_id - NUM_DIRECT_BLOCKS - INODE_PER_BLOCK) / INODE_PER_BLOCK;
            int second_level_index =
                (file_block_id - NUM_DIRECT_BLOCKS - INODE_PER_BLOCK) % INODE_PER_BLOCK;
            read_block(inode.double_inderect, block_buffer);
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
    file_t(int inode_num) : inode_num(inode_num), inode(get_inode(inode_num)) {
        pretty_logd("load inode %d", inode_num);
    }
    file_t(int inode_num, inode_t inode) : inode_num(inode_num), inode(inode) {
        pretty_logd("store inode %d", inode_num);
        write_inode(inode_num, &inode);
    }

    auto blocks() { return block_range_t{this}; }

    void append(uint32_t block_num) {
        asserts(
            block_num >= 0 && block_num < superblock.block_count, "append with invalid block_num");
        if (inode.blocks < NUM_DIRECT_BLOCKS) {
            inode.direct[inode.blocks] = block_num;
            inode.blocks++;
            write_inode(inode_num, &inode);
            return;
        }
        asserts(false, "only direct blocks supported");
    }

    void seek(int position) {
        asserts(position >= 0 && position <= inode.size, "seek to invalid position");
        pos = position;
        pretty_logd("seek to position %d", pos);
    }

    void write(void* data, int length) {
        uint8_t* src = (uint8_t*)data;
        int remaining = length;
        const int BLOCK_BYTES = SIZE_BLOCK * SECTOR_SIZE;

        while (remaining > 0) {
            pretty_logd("pos=%d, remaining=%d", pos, remaining);
            int file_block_id = pos / BLOCK_BYTES;
            int offset = pos % BLOCK_BYTES;
            uint32_t block_num;

            if (file_block_id >= inode.blocks) {
                int new_block = fs.block_map.alloc();
                asserts(new_block >= 0, "write: no free data block");
                // initialize new block to zero
                memset(block_buffer, 0, BLOCK_BYTES);
                append(new_block);
                block_num = find_block(file_block_id);
            } else {
                block_num = find_block(file_block_id);
                read_block(block_num, block_buffer);
            }

            int to_write = remaining;
            if (to_write > BLOCK_BYTES - offset) to_write = BLOCK_BYTES - offset;
            memcpy(block_buffer + offset, src, to_write);
            write_block(block_num, block_buffer);
            src += to_write;
            pos += to_write;
            remaining -= to_write;
        }

        if (pos > inode.size) {
            inode.size = pos;
            write_inode(inode_num, &inode);
        }
        pretty_logd("write complete, new size=%d", inode.size);
    }
};

void attach_dir(directory_t* dest, const char* name, int inode) {
    dentry_t* dest_entry = dest->entries + dest->num_entries;
    strcpy(dest_entry->name, name);
    dest_entry->inode_num = inode;
    dest->num_entries++;
}

void detach_dir(directory_t* dest, int index) {
    if (index < 0 || index >= dest->num_entries) {
        pretty_loge("detach_dir: index %d out of range", index);
        return;
    }
    if (index == dest->num_entries - 1) {
        dest->num_entries--;
        return;
    }
    dentry_t tmp = dest->entries[index];
    dest->entries[index] = dest->entries[dest->num_entries - 1];
    dest->entries[dest->num_entries - 1] = tmp;
    dest->num_entries--;
}

void mkdir(directory_t* dest, int parent_inode, const char* name, int inode) {
    memset(dest, 0, sizeof(directory_t));
    attach_dir(dest, ".", inode);
    attach_dir(dest, "..", parent_inode);
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
        .used_block = 1,  // root directory block
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
    if (bios_sd_write((kva_t)buffer, 1, sb.start_sector + sb.block_map_offset)) {
        pretty_logw("failed to write block bitmap to SD card");
        return 1;
    }

    inode_t root_inode = {
        .type = FS_TYPE_DIR,
        .link_count = 1,
        .size = sizeof(directory_t),
        .blocks = 1,
        .direct = {0},
        .indirect = 0,
        .double_inderect = 0,
    };
    write_inode(ROOT_INODE, &root_inode);
    static directory_t root_dir;
    mkdir(&root_dir, ROOT_INODE, ".", ROOT_INODE);
    write_block(0, &root_dir);

    pretty_logd("root directory created");
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

int get_child(directory_t* parent, const char* child) {
    for (int i = 0; i < parent->num_entries; i++) {
        dentry_t* entry = &parent->entries[i];
        if (strcmp(entry->name, child) == 0) {
            return entry->inode_num;
        }
    }
    return -1;  // not found
}

static int find_entry_index(directory_t* dir, const char* name) {
    for (int i = 0; i < dir->num_entries; i++) {
        if (strcmp(dir->entries[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int get_descendant(const char* path, int start_inode) {
    const char* start = path;
    static directory_t current_dir;
    int inode_num = start_inode;
    if (path[0] == '/') {
        inode_num = superblock.root_inode;
        start = path + 1;  // skip leading '/'
    }
    inode_t inode = get_inode(inode_num);
    read_block(inode.direct[0], &current_dir);

    auto descend = [&](const char* name) -> bool {
        if (strlen(name) == 0) return true;
        int next_inode = get_child(&current_dir, name);
        if (next_inode < 0) {
            pretty_logw("directory %s not found", name);
            return false;
        }
        inode_num = next_inode;
        inode = get_inode(inode_num);
        read_block(inode.direct[0], &current_dir);
        return true;
    };

    while (*path) {
        if (*path == '/') {
            if (!descend(start)) return -1;
            start = path + 1;
        }
        path++;
    }
    if (!descend(start)) return -1;
    return inode_num;
}

int do_cd(char* path) {
    int inode = get_descendant(path, current_running->cwd_inode);
    if (inode < 0) return 1;
    current_running->cwd_inode = inode;
    return 0;  // do_cd succeeds
}

static int split_parent_name(
    const char* path, char* out_parent, size_t parent_size, char* out_name, size_t name_size) {
    const char* last_slash = nullptr;
    int len = strlen(path);
    for (int i = len - 1; i >= 0; i--) {
        if (path[i] == '/') {
            last_slash = &path[i];
            break;
        }
    }

    if (last_slash == nullptr) {
        // relative to cwd
        if (parent_size > 0) out_parent[0] = '\0';
        if (strlen(path) >= name_size) return -1;
        strcpy(out_name, path);
        return 0;
    }

    if (last_slash == path) {
        // parent is root
        if (parent_size < 2) return -1;
        strcpy(out_parent, "/");
        if (strlen(last_slash + 1) >= name_size) return -1;
        strcpy(out_name, last_slash + 1);
        return 0;
    }

    int plen = last_slash - path;
    if ((size_t)plen >= parent_size) return -1;
    memcpy(out_parent, path, plen);
    out_parent[plen] = '\0';
    if (strlen(last_slash + 1) >= name_size) return -1;
    strcpy(out_name, last_slash + 1);
    return 0;
}

static int resolve_directory(
    const char* path, int start_inode, directory_t* out_dir, int* out_inode_num,
    inode_t* out_inode_meta) {
    int inode_num = start_inode;
    if (path != nullptr && path[0] != '\0') {
        inode_num = get_descendant(path, start_inode);
    }
    if (inode_num < 0) {
        pretty_logw("path %s not found", path);
        return -1;
    } else {
        pretty_logd("resolved path %s to inode %d", path ? path : "(cwd)", inode_num);
    }
    inode_t inode = get_inode(inode_num);
    if (!(inode.type & FS_TYPE_DIR)) {
        pretty_logw("inode %d is not a directory", inode_num);
        return -1;
    }
    read_block(inode.direct[0], out_dir);
    if (out_inode_num) *out_inode_num = inode_num;
    if (out_inode_meta) *out_inode_meta = inode;
    return 0;
}

static int resolve_parent_dir(
    const char* path, directory_t* out_parent_dir, int* out_parent_inode_num,
    inode_t* out_parent_inode, char* out_name, size_t name_size) {
    char parent_path[PATH_BUF_SIZE];
    if (split_parent_name(path, parent_path, sizeof(parent_path), out_name, name_size) != 0) {
        pretty_logd("invalid path %s", path);
        return -1;
    }
    if (out_name[0] == '\0') {
        pretty_logd("invalid name in path %s", path);
        return -1;
    }
    const char* dir_path = parent_path[0] == '\0' ? nullptr : parent_path;
    return resolve_directory(
        dir_path, current_running->cwd_inode, out_parent_dir, out_parent_inode_num,
        out_parent_inode);
}

int do_mkdir(char* path) {
    static directory_t parent_dir;
    inode_t parent_inode_meta;
    int parent_inode;
    char name[PATH_BUF_SIZE];
    if (resolve_parent_dir(
            path, &parent_dir, &parent_inode, &parent_inode_meta, name, sizeof(name)) != 0)
        return 1;

    int inode = fs.inode_map.alloc();
    if (inode < 0) return 1;

    static directory_t dir_data;
    mkdir(&dir_data, parent_inode, name, inode);
    file_t new_dir(inode, inode_t{.type = FS_TYPE_DIR, .link_count = 1, .size = 0, .blocks = 0});
    new_dir.seek(0);
    new_dir.write(&dir_data, sizeof(directory_t));

    attach_dir(&parent_dir, name, inode);
    write_block(parent_inode_meta.direct[0], &parent_dir);
    return 0;  // do_mkdir succeeds
}

int do_rmdir(char* path) {
    directory_t parent_dir;
    inode_t parent_inode_meta;
    int parent_inode;
    char name[PATH_BUF_SIZE];
    if (resolve_parent_dir(
            path, &parent_dir, &parent_inode, &parent_inode_meta, name, sizeof(name)) != 0)
        return 1;

    int found_index = find_entry_index(&parent_dir, name);
    if (found_index < 0) return 1;

    int child_inode_num = parent_dir.entries[found_index].inode_num;
    inode_t child = get_inode(child_inode_num);
    if (!(child.type & FS_TYPE_DIR)) {
        pretty_logw("%s is not a directory", name);
        return 1;
    }

    static directory_t child_dir;
    if (child.blocks > 0) {
        read_block(child.direct[0], &child_dir);
        if (child_dir.num_entries > 2) {
            pretty_logw("directory %s not empty", name);
            return 1;
        }
    }

    detach_dir(&parent_dir, found_index);
    write_block(parent_inode_meta.direct[0], &parent_dir);

    for (uint32_t i = 0; i < child.blocks && i < NUM_DIRECT_BLOCKS; i++) {
        int b = child.direct[i];
        if (b >= 0) fs.block_map.free(b);
    }
    fs.inode_map.free(child_inode_num);
    return 0;  // do_rmdir succeeds
}

int do_ls(char* path, int option) {
    // Note: argument 'option' serves for 'ls -l' in A-core

    static directory_t dir;
    int dir_inode;

    if (resolve_directory(
            (path == nullptr || path[0] != '\0') ? path : nullptr, current_running->cwd_inode, &dir,
            &dir_inode, nullptr) != 0) {
        return 1;
    }

    if (option & LS_VERBOSE) {

        display_table<dentry_t>(
            dir.entries, dir.num_entries, [](dentry_t* _) { return true; },
            table_entry_t{"INODE", 7, [](dentry_t* entry) { printkf("%d", entry->inode_num); }},
            table_entry_t{"TYPE", 6, [](dentry_t* entry) {
                inode_t inode = get_inode(entry->inode_num);
                if (inode.type & FS_TYPE_DIR) {
                    printkf("DIR");
                } else {
                    printkf("FILE");
                }
            }},
            table_entry_t{
                "SIZE", 6,
                [](dentry_t* entry) {
                    inode_t inode = get_inode(entry->inode_num);
                    printkf("%d", inode.size);
                }},
            table_entry_t{"NAME", 24, [](dentry_t* entry) { printkf("%s", entry->name); }});

    } else {
        for (int i = 0; i < dir.num_entries; i++) {
            printkf("%s  ", dir.entries[i].name);
            if ((i + 1) % 4 == 0) printkf("\n");
        }
    }
    screen_reflush();

    return 0;
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
