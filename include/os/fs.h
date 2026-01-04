#ifndef __INCLUDE_OS_FS_H__
#define __INCLUDE_OS_FS_H__

#include <os/task.h>
#include <type.h>
#include <static_assert.h>

/* macros of file system */
#define SUPERBLOCK_MAGIC 0xDF4C4459
#define NUM_FDESCS       16

/* data structures of file system */

// size: 512 bytes
typedef struct superblock {
    // TODO [P6-task1]: Implement the data structure of superblock
    uint32_t magic;
    uint32_t fs_size;
    uint32_t start_sector;
    uint32_t root_inode;
    uint32_t block_map_offset;
    uint32_t inode_map_offset;
    uint32_t inode_offset;
    uint32_t datablock_offset;
    uint32_t inode_count;
    uint32_t block_count;
    uint32_t used_inode;
    uint32_t used_block;
    uint8_t pad[512 - sizeof(uint32_t) * 12];
} superblock_t;

STATIC_ASSERT(sizeof(superblock_t) == 512, "superblock size incorrect");

// size: 32 bytes
typedef struct dentry {
    // TODO [P6-task1]: Implement the data structure of directory entry
    char name[28];
    uint32_t inode_num;
} dentry_t;

STATIC_ASSERT(sizeof(dentry_t) == 32, "dentry size incorrect");

#define FS_TYPE_DIR 0x4000

#define NUM_DIRECT_BLOCKS 10
// size: 64 bytes
typedef struct inode {
    // TODO [P6-task1]: Implement the data structure of inode
    uint32_t type;
    uint32_t link_count;
    uint32_t size; // in bytes
    uint32_t blocks;
    uint32_t direct[NUM_DIRECT_BLOCKS];
    uint32_t indirect;
    uint32_t double_inderect;
} inode_t;

STATIC_ASSERT(sizeof(inode_t) == 64, "inode size incorrect");

typedef struct fdesc {
    // TODO [P6-task2]: Implement the data structure of file descriptor
    uint32_t inode_num;
    uint32_t pos;
    uint32_t flags;
    uint32_t valid;
} fdesc_t;

#define FS_START_SECTOR (512 * 1024 * 1024 / SECTOR_SIZE)  // at 512MB
#define FS_END_SECTOR (1024 * 1024 * 1024 / SECTOR_SIZE)    // at 1GB

#define SIZE_INODE_MAP 1  // 1 sector
#define NUM_INODES     ((SIZE_INODE_MAP) * (SECTOR_SIZE) * 8)
#define INODE_PER_SECTOR (SECTOR_SIZE / sizeof(inode_t))
#define ROOT_INODE 0

STATIC_ASSERT(
    SECTOR_SIZE % sizeof(inode_t) == 0, "SECTOR_SIZE must be a multiple of inode_t size");

#define SIZE_INODE_TABLE (sizeof(inode_t) * NUM_INODES / SECTOR_SIZE)

#define SIZE_BLOCK_MAP 32

#define SIZE_BLOCK 8 // 4KB
#define INODE_PER_BLOCK (SIZE_BLOCK * SECTOR_SIZE / sizeof(inode_t))

#define MAX_DENTRIES (SIZE_BLOCK * SECTOR_SIZE / sizeof(dentry_t) - 1)

typedef struct directory {
    uint32_t num_entries;
    uint8_t pad[32 - sizeof(uint32_t)];
    dentry_t entries[MAX_DENTRIES];
} directory_t;

STATIC_ASSERT(sizeof(directory_t) == SIZE_BLOCK * SECTOR_SIZE, "directory size incorrect");

/* modes of do_open */
#define O_RDONLY 1 /* read only open */
#define O_WRONLY 2 /* write only open */
#define O_RDWR   3 /* read/write open */

/* whence of do_lseek */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define LS_VERBOSE (1 << 0) /* for 'ls -l' */

/* fs function declarations */
extern int do_mkfs(void);
extern int do_statfs(void);
extern int do_cd(char* path);
extern int do_mkdir(char* path);
extern int do_rmdir(char* path);
extern int do_ls(char* path, int option);
extern int do_open(char* path, int mode);
extern int do_read(int fd, char* buff, int length);
extern int do_write(int fd, char* buff, int length);
extern int do_close(int fd);
extern int do_ln(char* src_path, char* dst_path);
extern int do_rm(char* path);
extern int do_lseek(int fd, int offset, int whence);

#endif
