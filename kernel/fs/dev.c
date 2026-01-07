#include <os/fs.h>
#include <os/string.h>

device_t devices[NUM_DEVICES];
int device_count;

static const char* cache_policy_name[] = {
    "write back",
    "write through",
};

int device_read_vm(inode_t* inode, void* dest, kva_t pgdir, uint32_t offset, uint32_t length) {
    static char buffer[1024];
    int size = snprintf(
        buffer, 1024, "page_cache_policy = %s\nwrite_back_freq = %d\n",
        cache_policy_name[pagecache_config.policy], pagecache_config.write_back_freq);
    if (offset >= size) {
        return 0;
    }
    if (offset + length > size) {
        length = size - offset;
    }
    if (pgdir) {
        memcpy_kva2uva((uva_t)dest, (kva_t)(buffer + offset), length, pgdir);
    } else {
        memcpy(dest, buffer + offset, length);
    }
    return length;
}

int device_write_vm(inode_t* inode, void* src, kva_t pgdir, uint32_t offset, uint32_t length) {
    return 0;  // not writable
}

void init_fs_device() {
    devices[0] = (device_t){
        .read = device_read_vm,
        .write = device_write_vm,
    };
    device_count = 1;

    do_mkdir("/proc");
    do_mkdir("/proc/sys");
    inode_t* vm_dev = path_create("/proc/sys/vm", FS_TYPE_DEV);
    inode_open(vm_dev);
    vm_dev->device_id = 0;
    inode_close(vm_dev);
}
