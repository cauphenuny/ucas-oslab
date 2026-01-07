#include <tui.hpp>

extern "C" {
#include <logger.h>
#include <os/errno.h>
#include <os/fs.h>
#include <os/kernel.h>
#include <os/string.h>
#include <os/task.h>
#include <type.h>
}

int do_mkfs(void) {
    // TODO [P6-task1]: Implement do_mkfs

    dcache_reset();

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
        .inode_offset = inode_table,
        .datablock_offset = data,
        .inode_count = NUM_INODES,
        .block_count = fs_size / NSECTOR_BLOCK - data,
        .used_inode = 0,  // root inode
        .used_block = 0,  // root directory block
    };
    pretty_logd(
        "created superblock, fs_size: %d, start_sector: %d", superblock.fs_size,
        superblock.start_sector);
    pretty_logd("block_map_offset: %dd", superblock.block_map_offset);
    pretty_logd(
        "inode_offset: %d, data_offset: %d", superblock.inode_offset, superblock.datablock_offset);
    pretty_logd("inode_count: %d, block_count: %d", superblock.inode_count, superblock.block_count);

    if (bios_sd_write((kva_t)&superblock, 1, FS_START_SECTOR)) {
        pretty_logw("failed to write superblock to SD card");
        return 1;
    }
    for (uint32_t bnum = superblock.block_map_offset; bnum < superblock.inode_offset; bnum++) {
        block_memset(bnum, 0x00);  // clear block map
    }
    pretty_logd("block map cleared");

    inode_t* root_inode = inode_alloc(FS_TYPE_DIR);
    asserts(root_inode->inode_num == ROOT_INODE, "root inode number incorrect");

    inode_open(root_inode);
    root_inode->link_count = 1;
    asserts(dir_link(root_inode, ".", ROOT_INODE) == 0, "failed to link . in root directory");
    asserts(dir_link(root_inode, "..", ROOT_INODE) == 0, "failed to link .. in root directory");
    root_inode->link_count++;
    inode_close(root_inode);

    pretty_logd("root directory created");
    return 0;  // do_mkfs succeeds
}

void init_fs() {
    init_inodes();
    init_dentry_cache();
    init_fs_cache();

    if (bios_sd_read((kva_t)&superblock, 1, FS_START_SECTOR)) {
        pretty_loge("failed to read superblock from sd-card");
    }
    if (superblock.magic != SUPERBLOCK_MAGIC) {
        pretty_logw(
            "invalid filesystem magic number: expected 0x%x, got 0x%x", SUPERBLOCK_MAGIC,
            superblock.magic);
        do_mkfs();
        pretty_log(
            LOG_INFO, "filesystem init: size=%d sectors, inode_count=%d, block_count=%d",
            superblock.fs_size, superblock.inode_count, superblock.block_count);
    } else {
        pretty_log(
            LOG_INFO, "filesystem exists: size=%d sectors, inode_count=%d, block_count=%d",
            superblock.fs_size, superblock.inode_count, superblock.block_count);
    }

    init_fs_etc();
}

superblock_t superblock;

int do_statfs(void) {
    // TODO [P6-task1]: Implement do_statfs
    printk("Filesystem Infomation:\n");
    if (superblock.magic != SUPERBLOCK_MAGIC) {
        printk("<uninitialized>\n");
        return 1;
    }
    printk("Total size: %d blocks at 0x%x\n", superblock.fs_size, superblock.start_sector);
    printk("Block map: %d blocks at #%d\n", NBLOCK_BLOCK_MAP, superblock.block_map_offset);
    printk(
        "Inodes: %d entries, %d blocks at #%d, used: %d(%d%%)\n", NUM_INODES, NBLOCK_INODE_TABLE,
        superblock.inode_offset, superblock.used_inode,
        superblock.used_inode * 100 / superblock.inode_count);
    printk(
        "Data blocks: %d entries at #%d, used: %d(%d%%)\n", superblock.block_count,
        superblock.datablock_offset, superblock.used_block,
        superblock.used_block * 100 / superblock.block_count);
    printk(
        "Cache policy: %s, freq: %ds\n",
        pagecache_config.policy == POLICY_WRITE_THROUGH ? "write-through" : "write-back",
        pagecache_config.write_back_freq);
    return 0;  // do_statfs succeeds
}

int do_cd(char* path) {
    inode_t* target = path_resolve_entry(path);
    if (!target) return ERR_FILE_NOT_EXISTS;
    current_running->cwd_inode = target->inode_num;
    inode_deref(target);
    return 0;  // do_cd succeeds
}

int do_mkdir(char* path) {
    inode_t* inode = path_create(path, FS_TYPE_DIR);
    if (inode == NULL) {
        return ERR_FILE_NOT_EXISTS;
    }
    inode_deref(inode);
    return 0;
}

int do_rmdir(char* path) { return path_remove(path, 1); }

class directory_t {
    inode_t* inode;
    dentry_t dentry;

public:
    directory_t(inode_t* inode) : inode(inode) {}
    size_t size() { return inode->size / sizeof(dentry_t); }
    dentry_t& operator[](size_t index) {
        asserts(index < size(), "directory index out of range");
        int n = inode_read(inode, &dentry, 0, index * sizeof(dentry_t), sizeof(dentry_t));
        asserts(n == sizeof(dentry_t), "short read");
        return dentry;
    }
};

int do_ls(char* path, int option) {
    // Note: argument 'option' serves for 'ls -l' in A-core

    const char* wrapped_path = (path == nullptr || path[0] == '\0') ? "" : path;

    inode_t* dir_inode = path_resolve_entry(wrapped_path);

    if (dir_inode == nullptr) {
        pretty_logw("cannot access '%s': No such file or directory", wrapped_path);
        return ERR_FILE_NOT_EXISTS;
    }

    inode_open(dir_inode);

    if (dir_inode->type != FS_TYPE_DIR) {
        pretty_logw("cannot access '%s': Not a directory", wrapped_path);
        inode_close(dir_inode);
        return ERR_FILE_TYPE_MISMATCH;
    }

    directory_t dir(dir_inode);

    if (option & LS_VERBOSE) {
        display_table<dentry_t>(
            dir, dir.size(), [](const dentry_t* entry) { return entry->inode_num != 0; },
            table_entry_t{
                "INODE", 7, [](const dentry_t* entry) { printkf("%d", entry->inode_num); }},
            table_entry_t{
                "SIZE",
                10,
                [dir_inode](const dentry_t* entry) {
                    inode_t* ind = inode_ref(entry->inode_num);
                    if (ind != dir_inode) inode_open(ind);
                    printkf("%d", ind->size);
                    if (ind != dir_inode) inode_close(ind);
                },
            },
            table_entry_t{
                "TYPE", 6,
                [dir_inode](const dentry_t* entry) {
                    inode_t* ind = inode_ref(entry->inode_num);
                    if (ind != dir_inode) inode_open(ind);
                    printkf(
                        "%s", ind->type == FS_TYPE_DIR   ? "DIR"
                              : ind->type == FS_TYPE_DEV ? "DEV"
                                                         : "FILE");
                    if (ind != dir_inode) inode_close(ind);
                }},
            table_entry_t{
                "NAME",
                24,
                [dir_inode](const dentry_t* entry) {
                    inode_t* ind = inode_ref(entry->inode_num);
                    if (ind != dir_inode) inode_open(ind);
                    printkf("%s%s", entry->name, ind->type == FS_TYPE_DIR ? "/" : "");
                    if (ind != dir_inode) inode_close(ind);
                },
            });

    } else {
        for (size_t i = 0; i < dir.size(); i++) {
            if (dir[i].inode_num == 0) {
                continue;
            }
            inode_t* ind = inode_ref(dir[i].inode_num);
            if (ind != dir_inode) inode_open(ind);
            printkf("%s%s  ", dir[i].name, ind->type == FS_TYPE_DIR ? "/" : "");
            if (ind != dir_inode) inode_close(ind);
        }
        printkf("\n");
    }
    screen_reflush();

    inode_close(dir_inode);

    return 0;
}

static fdesc_t fdesc_array[NUM_FDESCS];

int do_open(char* path, int mode) {
    int writable = (mode & O_WRONLY) || (mode & O_RDWR);
    int readable = (mode & O_RDONLY) || (mode & O_RDWR);

    inode_t* inode = NULL;

    if (writable) {
        inode = path_create(path, FS_TYPE_FILE);
    }
    if (!inode) inode = path_resolve_entry(path);

    if (inode == NULL) {
        return -1;
    }

    inode_open(inode);

    if (inode->type == FS_TYPE_DIR) {
        pretty_logw("cannot open a directory");
        inode_close(inode);
        return -1;
    }

    int fd = -1;
    for (int i = 0; i < NUM_MAX_PROC_FD; i++) {
        if (!current_running->fd_table[i]) {
            fd = i;
            break;
        }
    }
    if (fd == -1) {
        pretty_logw("too many open files in current process");
        inode_close(inode);
        return -1;
    }

    for (int i = 0; i < NUM_FDESCS; i++) {
        if (fdesc_array[i].valid == 0) {
            fdesc_array[i].inode = inode;
            fdesc_array[i].rpos = 0;
            fdesc_array[i].wpos = 0;
            fdesc_array[i].writable = writable;
            fdesc_array[i].readable = readable;
            fdesc_array[i].valid = 1;
            current_running->fd_table[fd] = &fdesc_array[i];
            inode_unlock(inode);
            return fd;
        }
    }

    pretty_logw("no free file descriptor available");
    inode_close(inode);
    return -1;
}

int do_read(int fd, char* buff, int length) {
    if (fd < 0 || fd >= NUM_MAX_PROC_FD) {
        return 0;
    }
    auto fp = current_running->fd_table[fd];
    if (!fp) {
        return 0;
    }
    inode_open(fp->inode);
    int ret = inode_read(fp->inode, buff, current_running->pgdir, fp->rpos, length);
    fp->rpos += ret;
    inode_unlock(fp->inode);
    return ret;
}

int do_write(int fd, char* buff, int length) {
    if (fd < 0 || fd >= NUM_MAX_PROC_FD) {
        return 0;
    }
    auto fp = current_running->fd_table[fd];
    if (!fp) {
        return 0;
    }
    inode_open(fp->inode);
    int ret = inode_write(fp->inode, buff, current_running->pgdir, fp->wpos, length);
    fp->wpos += ret;
    inode_unlock(fp->inode);
    return ret;
}

int do_close(int fd) {
    if (fd < 0 || fd >= NUM_MAX_PROC_FD) {
        return 0;
    }
    fdesc_t* fdesc = current_running->fd_table[fd];
    if (!fdesc) {
        return ERR_FD_INVALID;
    }
    inode_deref(fdesc->inode);
    fdesc->valid = 0;
    current_running->fd_table[fd] = NULL;
    return 0;  // do_close succeeds
}

int do_ln(char* src_path, char* dst_path) {
    inode_t* src_inode = path_resolve_entry(src_path);
    if (!src_inode) {
        return ERR_FILE_NOT_EXISTS;
    }
    inode_open(src_inode);
    if (src_inode->type == FS_TYPE_DIR) {
        inode_close(src_inode);
        return ERR_FILE_TYPE_MISMATCH;
    }

    char name[MAX_FILE_NAME];
    inode_t* parent = path_resolve_parent(dst_path, name);
    if (!parent) {
        inode_close(src_inode);
        return ERR_FILE_NOT_EXISTS;
    }

    inode_open(parent);
    int ret = dir_link(parent, name, src_inode->inode_num);
    inode_close(parent);
    inode_close(src_inode);
    return ret;
}

int do_rm(char* path) {
    char name[MAX_FILE_NAME];
    inode_t* dir = path_resolve_parent(path, name);
    if (!dir) {
        return ERR_FILE_NOT_EXISTS;
    }

    inode_open(dir);
    int ret = dir_unlink(dir, name);
    inode_close(dir);
    return ret;
}

int do_lseek(int fd, int offset, int whence) {
    if (fd < 0 || fd >= NUM_MAX_PROC_FD) {
        pretty_logw("invalid fd %d", fd);
        return ERR_FD_INVALID;
    }
    pretty_logi("try seek fd=%d offset=%d whence=%d", fd, offset, whence);
    auto fp = current_running->fd_table[fd];
    if (!fp) {
        return ERR_FD_INVALID;
    }

    int target = max(fp->rpos, fp->wpos);
    inode_open(fp->inode);
    switch (whence) {
        case SEEK_SET: target = offset; break;
        case SEEK_CUR: target += offset; break;
        case SEEK_END: target = fp->inode->size + offset; break;
        default:;
    }
    if (target < 0) target = 0;
    inode_unlock(fp->inode);

    pretty_logi("lseek fd=%d to %d (whence=%d, offset=%d)", fd, target, whence, offset);

    fp->wpos = target;
    fp->rpos = min(target, fp->inode->size);
    return target;
}

void flush_filesystem() {
    bios_sd_write((kva_t)&superblock, 1, FS_START_SECTOR);
    flush_block_cache();
    flush_fs_cache();
}

void shutdown_fs() { flush_filesystem(); }
