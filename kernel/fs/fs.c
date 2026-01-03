#include <logger.h>
#include <os/fs.h>
#include <os/kernel.h>
#include <os/string.h>
#include <os/task.h>

static fdesc_t fdesc_array[NUM_FDESCS];
static superblock_t superblock;

int do_mkfs(void) {
    // TODO [P6-task1]: Implement do_mkfs
    uint32_t block_map = 1;  // superblock is at sector 0
    uint32_t inode_map = block_map + SIZE_BLOCK_MAP;
    uint32_t inode_table = inode_map + SIZE_INODE_MAP;
    uint32_t data = inode_table + SIZE_INODE_TABLE;
    uint32_t fs_size = FS_END_SECTOR - FS_START_SECTOR;

    superblock = (superblock_t){
        .magic = SUPERBLOCK_MAGIC,
        .fs_size = FS_END_SECTOR - FS_START_SECTOR,
        .start_sector = FS_START_SECTOR,
        .root_inode = 0,
        .block_map_offset = block_map,
        .inode_map_offset = inode_map,
        .inode_offset = inode_table,
        .data_offset = data,
        .inode_count = NUM_INODES,
        .block_count = (fs_size - data) / SIZE_BLOCK,
        .used_inode = 1,  // root inode
        .used_block = 0,
    };
    pretty_logd(
        "created superblock, fs_size: %d, start_sector: %d", superblock.fs_size,
        superblock.start_sector);
    pretty_logd(
        "block_map_offset: %d, inode_map_offset: %d", superblock.block_map_offset,
        superblock.inode_map_offset);
    pretty_logd(
        "inode_offset: %d, data_offset: %d", superblock.inode_offset, superblock.data_offset);
    pretty_logd("inode_count: %d, block_count: %d", superblock.inode_count, superblock.block_count);

    if (bios_sd_write((kva_t)&superblock, 1, FS_START_SECTOR)) {
        pretty_logw("failed to write superblock to SD card");
        return 1;
    }
    return 0;  // do_mkfs succeeds
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
        superblock.data_offset, superblock.used_block,
        superblock.used_block * 100 / superblock.block_count);
    return 0;  // do_statfs succeeds
}

int do_cd(char* path) {
    // TODO [P6-task1]: Implement do_cd

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
