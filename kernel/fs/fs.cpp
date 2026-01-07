#include <tui.hpp>

extern "C" {
#include <logger.h>
#include <os/fs.h>
#include <os/kernel.h>
#include <os/string.h>
#include <os/task.h>
}

static fdesc_t fdesc_array[NUM_FDESCS];

int do_mkfs(void) {
    // TODO [P6-task1]: Implement do_mkfs

    uint32_t block_map = 1;  // superblock is at block 0
    uint32_t inode_map = block_map + NBLOCK_BLOCK_MAP;
    uint32_t inode_table = inode_map + NBLOCK_INODE_MAP;
    uint32_t data = inode_table + NBLOCK_INODE_TABLE;
    uint32_t fs_size = FS_END_SECTOR - FS_START_SECTOR;

    superblock = (superblock_t){
        .magic = SUPERBLOCK_MAGIC,
        .fs_size = FS_END_SECTOR - FS_START_SECTOR,
        .start_sector = FS_START_SECTOR,
        .root_inode = ROOT_INODE,
        .block_map_offset = block_map,
        .inode_map_offset = inode_map,
        .inode_offset = inode_table,
        .datablock_offset = data,
        .inode_count = NUM_INODES,
        .block_count = (fs_size - data) / BLOCK_SIZE,
        .used_inode = 0,  // root inode
        .used_block = 0,  // root directory block
    };
    pretty_logd(
        "created superblock, fs_size: %d, start_sector: %d", superblock.fs_size,
        superblock.start_sector);
    pretty_logd(
        "block_map_offset: %d, inode_map_offset: %d", superblock.block_map_offset,
        superblock.inode_map_offset);
    pretty_logd(
        "inode_offset: %d, data_offset: %d", superblock.inode_offset, superblock.datablock_offset);
    pretty_logd("inode_count: %d, block_count: %d", superblock.inode_count, superblock.block_count);

    if (bios_sd_write((kva_t)&superblock, 1, FS_START_SECTOR)) {
        pretty_logw("failed to write superblock to SD card");
        return 1;
    }
    for (uint32_t bnum = superblock.block_map_offset; bnum < superblock.inode_map_offset; bnum++) {
        block_memset(bnum, 0x00);  // clear block map
    }
    for (uint32_t bnum = superblock.inode_map_offset; bnum < superblock.inode_offset; bnum++) {
        block_memset(bnum, 0x00);  // clear inode map
    }

    inode_t* root_inode = inode_alloc(FS_TYPE_DIR);
    asserts(root_inode->inode_num == ROOT_INODE, "root inode number incorrect");

    inode_open(root_inode);
    dir_link(root_inode, ".", ROOT_INODE);
    dir_link(root_inode, "..", ROOT_INODE);
    inode_close(root_inode);

    pretty_logd("root directory created");
    return 0;  // do_mkfs succeeds
}

superblock_t init_fs() {
    init_inodes();
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

superblock_t superblock = init_fs();

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
    printk("Block map: %d sectors at #%d\n", NBLOCK_BLOCK_MAP, superblock.block_map_offset);
    printk("Inode map: %d sectors at #%d\n", NBLOCK_INODE_MAP, superblock.inode_map_offset);
    printk(
        "Inode table: %d entries, %d sectors at #%d, used: %d(%d%%)\n", NUM_INODES,
        NBLOCK_INODE_TABLE, superblock.inode_offset, superblock.used_inode,
        superblock.used_inode * 100 / superblock.inode_count);
    printk(
        "Data blocks: %d entries at #%d, used: %d(%d%%)\n", superblock.block_count,
        superblock.datablock_offset, superblock.used_block,
        superblock.used_block * 100 / superblock.block_count);
    return 0;  // do_statfs succeeds
}

int do_cd(char* path) {
    inode_t* target = path_resolve_entry(path);
    if (!target) return -1;
    current_running->cwd_inode = target->inode_num;
    inode_deref(target);
    return 0;  // do_cd succeeds
}

int do_mkdir(char* path) {
    inode_t* inode = path_create(path, FS_TYPE_DIR);
    if (inode == NULL) {
        return -1;
    }
    inode_deref(inode);
    return 0;
}

int do_rmdir(char* path) { return path_remove(path, 1); }

class directory_t {
    inode_t* inode;

public:
    directory_t(inode_t* inode) : inode(inode) {}
    size_t size() { return inode->size / sizeof(dentry_t); }
    dentry_t operator[](size_t index) {
        asserts(index < size(), "directory index out of range");
        dentry_t dentry;
        int n = inode_read(inode, &dentry, 0, index * sizeof(dentry_t), sizeof(dentry_t));
        asserts(n == sizeof(dentry_t), "short read");
        return dentry;
    }
};

int do_ls(char* path, int option) {
    // Note: argument 'option' serves for 'ls -l' in A-core

    const char* wrapped_path = (path == nullptr || path[0] == '\0') ? "." : path;

    inode_t* dir_inode = path_resolve_entry(wrapped_path);

    if (dir_inode == nullptr) {
        pretty_logw("cannot access '%s': No such file or directory", wrapped_path);
        return 1;
    }

    if (dir_inode->type != FS_TYPE_DIR) {
        pretty_logw("cannot access '%s': Not a directory", wrapped_path);
        inode_deref(dir_inode);
        return 2;
    }

    directory_t dir(dir_inode);

    if (option & LS_VERBOSE) {
        display_table<dentry_t>(
            dir, dir.size(), [](const dentry_t* _) { return true; },
            table_entry_t{
                "INODE", 7, [](const dentry_t* entry) { printkf("%d", entry->inode_num); }},
            table_entry_t{
                "TYPE", 6,
                [](const dentry_t* entry) {
                    inode_t* ind = inode_ref(entry->inode_num);
                    printkf(
                        "%s", ind->type == FS_TYPE_DIR   ? "DIR"
                              : ind->type == FS_TYPE_DEV ? "DEV"
                                                         : "FILE");
                    inode_deref(ind);
                }},
            table_entry_t{
                "SIZE", 6,
                [](const dentry_t* entry) {
                    inode_t* ind = inode_ref(entry->inode_num);
                    printkf("%d", ind->size);
                }},
            table_entry_t{"NAME", 24, [](const dentry_t* entry) { printkf("%s", entry->name); }});

    } else {
        for (size_t i = 0; i < dir.size(); i++) {
            printkf("%s  ", dir[i].name);
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
