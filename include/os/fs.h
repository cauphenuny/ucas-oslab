#ifndef __INCLUDE_OS_FS_H__
#define __INCLUDE_OS_FS_H__

#include <os/lock.h>
#include <os/task.h>
#include <static_assert.h>
#include <type.h>

void init_fs();

/* macros of file system */
#define SUPERBLOCK_MAGIC 0xDF4C4459
#define NUM_FDESCS       32

/* data structures of file system */

// size: 512 bytes
typedef struct superblock {
    // TODO [P6-task1]: Implement the data structure of superblock
    uint32_t magic;
    uint32_t fs_size;
    uint32_t start_sector;
    uint32_t root_inode;
    uint32_t block_map_offset;  // inode_map inode.type == 0
    uint32_t inode_offset;
    uint32_t datablock_offset;
    uint32_t inode_count;
    uint32_t block_count;
    uint32_t used_inode;
    uint32_t used_block;
    uint8_t pad[512 - sizeof(uint32_t) * 11];
} superblock_t;

extern superblock_t superblock;

STATIC_ASSERT(sizeof(superblock_t) == 512, "superblock size incorrect");

#define MAX_FILE_NAME 28

// size: 32 bytes
typedef struct dentry {
    // TODO [P6-task1]: Implement the data structure of directory entry
    char name[MAX_FILE_NAME];
    uint32_t inode_num;
} dentry_t;

STATIC_ASSERT(sizeof(dentry_t) == 32, "dentry size incorrect");

#define FS_TYPE_DIR  0x01
#define FS_TYPE_FILE 0x02
#define FS_TYPE_DEV  0x04

#define NUM_DIRECT_BLOCKS          10
#define NUM_INDIRECT_BLOCKS        (BLOCK_SIZE / sizeof(int32_t))
#define NUM_DOUBLE_INDIRECT_BLOCKS (NUM_INDIRECT_BLOCKS * NUM_INDIRECT_BLOCKS)

// size: 64 bytes
typedef struct diskinode {
    // TODO [P6-task1]: Implement the data structure of inode
    uint16_t type;
    uint16_t link_count;  // reference in filesystem
    uint32_t inode_num;
    uint32_t size;  // in bytes
    uint32_t blocks;
    int32_t direct[NUM_DIRECT_BLOCKS];
    int32_t indirect;
    int32_t double_indirect;
} diskinode_t;

// NOTE: DO NOT change the layout (make it synchorous to diskinode_t)
typedef struct inode {
    uint16_t type;
    uint16_t link_count;  // reference in filesystem
    uint32_t inode_num;
    uint32_t size;  // in bytes
    uint32_t blocks;
    int32_t direct[NUM_DIRECT_BLOCKS];
    int32_t indirect;
    int32_t double_indirect;
    uint16_t valid;
    uint16_t ref_count;  // reference in memory
    mutex_lock_t lock;
} inode_t;

STATIC_ASSERT(sizeof(diskinode_t) == 64, "inode size incorrect");

typedef struct fdesc {
    // TODO [P6-task2]: Implement the data structure of file descriptor
    inode_t* inode;
    uint32_t valid;
    uint32_t pos;
    uint8_t readable;
    uint8_t writable;
} fdesc_t;

#define FS_START_SECTOR (512 * 1024 * 1024 / SECTOR_SIZE)   // at 512MB
#define FS_END_SECTOR   (1024 * 1024 * 1024 / SECTOR_SIZE)  // at 1GB

#define ROOT_INODE 1

#define NSECTOR_BLOCK 8  // 4KB, size of block in sectors

#define BLOCK_SIZE (NSECTOR_BLOCK * SECTOR_SIZE)

#define NBLOCK_INODE_MAP 1                                      // 1 sector
#define NUM_INODES       ((NBLOCK_INODE_MAP) * BLOCK_SIZE * 8)  // all inodes in filesystem
#define INODE_PER_BLOCK  (BLOCK_SIZE / sizeof(diskinode_t))     // inode(content) per sector

STATIC_ASSERT(
    BLOCK_SIZE % sizeof(diskinode_t) == 0, "SECTOR_SIZE must be a multiple of inode_t size");

#define NBLOCK_INODE_TABLE \
    (sizeof(diskinode_t) * NUM_INODES / BLOCK_SIZE)  // size of inode table in blocks

#define NBLOCK_BLOCK_MAP 32  // size of block map in sectors

#define MAX_DENTRIES (BLOCK_SIZE / sizeof(dentry_t) - 1)

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

typedef struct block {
    int valid;
    int block_num;
    int refcnt;
    uint8_t data[BLOCK_SIZE];
} block_t;

block_t* block_open(int block_num);
void block_memset(int block_num, uint8_t val);
void show_blocks();
void block_close(block_t* blk);
int block_alloc(void);
int block_allocset(uint8_t val);
void block_free(int block_num);
void flush_block_cache();
void shutdown_fs();

#define INODE2BLOCK(inode_num)  (superblock.inode_offset + (inode_num) / INODE_PER_BLOCK)
#define INODE2OFFSET(inode_num) ((inode_num) % INODE_PER_BLOCK)

#define BLOCKID2MAPBLOCK(block_num)  (superblock.block_map_offset + (block_num) / (BLOCK_SIZE * 8))
#define BLOCKID2MAPOFFSET(block_num) ((block_num) % (BLOCK_SIZE * 8))

#define DATABLOCK(block_num) (superblock.datablock_offset + (block_num))

void init_inodes();

// NOTE: these returns an unlocked but referenced node
inode_t* inode_alloc(int type);
inode_t* inode_ref(int inode_num);

// NOTE: these do not require inode locked
void inode_deref(inode_t* inode);

// NOTE: these requires inode locked
void inode_clear(inode_t* inode);
void inode_sync(inode_t* inode);
int inode_mapblock(inode_t* inode, int block_in_file);
void inode_delete(inode_t* inode);
int inode_read(inode_t* inode, void* dest, kva_t pgdir, uint32_t offset, uint32_t length);
int inode_write(inode_t* inode, void* src, kva_t pgdir, uint32_t offset, uint32_t length);

// NOTE: these requires inode locked and unlocks inode
void inode_unlock(inode_t* inode);
void inode_close(inode_t* inode);

// NOTE: these locks inode
void inode_open(inode_t* inode);

// NOTE: these requires a locked inode
[[nodiscard]] int dir_link(inode_t* dir, const char* filename, int inode_num);
[[nodiscard]] int dir_unlink(inode_t* dir, const char* filename);
[[nodiscard]] int dir_rmdir(inode_t* dir, const char* dirname);

// NOTE: these below returns an unlocked but referenced inode
inode_t* dir_lookup(inode_t* dir, const char* filename, size_t* poff);
inode_t* path_resolve_entry(const char* path);
inode_t* path_resolve_parent(const char* path, char* name);
inode_t* path_create(const char* path, int type);
int path_remove(const char* path, int isdir);

enum {
    POLICY_WRITE_BACK = 0,
    POLICY_WRITE_THROUGH,
};

typedef struct {
    int policy;
    int cache_size;
    int write_back_freq;
} cache_config_t;

extern cache_config_t pagecache_config;

void cached_block_read(void* dest, int block_num);
void cached_block_write(void* dest, int block_num);
void init_fs_cache();
void flush_fs_cache();
void cache_routine();

#endif
