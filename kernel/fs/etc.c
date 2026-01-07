#include <logger.h>
#include <os/fs.h>
#include <os/string.h>
#include <sys/syscall.h>

static bool isspace(char ch) {
    return ch == ' ' || ch == '\n' || ch == '\t' || ch == '\v' || ch == '\f' || ch == '\r';
}

static bool isdigit(char ch) { return '0' <= ch && ch <= '9'; }

/**
 * Convert a string into a decimal long
 */
static long atol(const char* str) {
    long ret = 0;
    int negative = 0;
    int base = 10;

    // Check if str pointer is NULL
    if (NULL == str) {
        return 0;
    }

    // Skip blanks until reaching the first non-blank char
    while (isspace(*str)) {
        ;
    }
    if ('+' == *str) {
        negative = 0;
        ++str;
    } else if ('-' == *str) {
        negative = 1;
        ++str;
    } else if (isdigit(*str)) {
        negative = 0;
    } else {
        return 0;
    }

    // 0x or 0X for hexadecimal
    if ((str[0] == '0' && str[1] == 'x') || (str[0] == '0' && str[1] == 'X')) {
        base = 16;
        ++str;
        ++str;
    }

    // Start converting ...
    while (*str != '\0') {
        if (isdigit(*str)) {
            ret = ret * base + (*str - '0');
        } else if (base == 16) {
            if ('a' <= *str && *str <= 'f') {
                ret = ret * base + (*str - 'a' + 10);
            } else if ('A' <= *str && *str <= 'F') {
                ret = ret * base + (*str - 'A' + 10);
            } else {
                return 0;
            }
        } else {
            return 0;
        }
        ++str;
    }

    return negative ? -ret : ret;
}

/**
 * Convert a string into a decimal int
 */
static int atoi(const char* str) { return (int)atol(str); }

device_t devices[NUM_DEVICES];
int device_count;

static const char* cache_policy_name[] = {
    "write back",
    "write through",
};

static void etc_vm_reset(inode_t* inode) {
    static char buffer[1024];
    int size = snprintf(
        buffer, 1024, "page_cache_policy = %s\nwrite_back_freq = %d\n",
        cache_policy_name[pagecache_config.policy], pagecache_config.write_back_freq);
    inode_write(inode, (void*)buffer, 0, 0, size);
}

static int etc_vm_parse(inode_t* inode, cache_config_t* dest) {
    if (!dest) return -1;
    static char buffer[1024];
    int size = inode_read(inode, (void*)buffer, 0, 0, 1024);
    const char* line0 = buffer;
    for (int i = 1; i < size; i++) {
        if (buffer[i] == '\n' || buffer[i] == '\0') {
            buffer[i] = '\0';
        }
    }

    const char* line1 = min(line0 + strlen(line0) + 1, buffer + size);

    pretty_logd("line0: '%s'", line0);
    pretty_logd("line1: '%s'", line1);

    if (strncmp(line0, "page_cache_policy = write back", 30) == 0) {
        dest->policy = POLICY_WRITE_BACK;
    } else if (strncmp(line0, "page_cache_policy = write through", 33) == 0) {
        dest->policy = POLICY_WRITE_THROUGH;
    } else {
        pretty_logw("etc/vm: invalid cache policy");
        return -1;
    }
    pretty_logd("etc/vm: set cache policy = %s", cache_policy_name[dest->policy]);

    if (strncmp(line1, "write_back_freq = ", 18) == 0) {
        dest->write_back_freq = atoi(line1 + 18);
    } else {
        pretty_logw("etc/vm: invalid write_back_freq");
        return -1;
    }

    pretty_logd("etc/vm: set write_back_freq = %d", dest->write_back_freq);
    return 0;
}

static void etc_vm_daemon() {
    set_process_nice(10, current_running->pid);
    while (true) {
        inode_t* inode = path_resolve_entry("/proc/sys/vm");
        inode_open(inode);
        cache_config_t config;
        if (etc_vm_parse(inode, &config) == 0) {
            pagecache_config.policy = config.policy;
            pagecache_config.write_back_freq = config.write_back_freq;
        } else {
            pretty_logw("etc/vm: parse error, reset to default");
            etc_vm_reset(inode);
        }
        inode_close(inode);
        do_sleep(1);
    }
}

static void etc_fs_reset(inode_t* inode) {
    static char buffer[128];
    int size = snprintf(
        buffer, sizeof(buffer), "dentry_cache = %s\nflush = 0\n",
        dcache_is_enabled() ? "on" : "off");
    inode_write(inode, (void*)buffer, 0, 0, size);
}

static int etc_fs_parse(inode_t* inode) {
    static char buffer[128];
    int size = inode_read(inode, (void*)buffer, 0, 0, sizeof(buffer) - 1);
    if (size <= 0) return -1;
    if (size >= (int)sizeof(buffer)) size = sizeof(buffer) - 1;
    buffer[size] = '\0';

    char* line2 = buffer;
    while (*line2 && *line2 != '\n') line2++;
    if (*line2 == '\n') {
        *line2 = '\0';
        line2++;
    }
    while (*line2 == '\n') line2++;

    bool enable;
    if (strncmp(buffer, "dentry_cache = on", 18) == 0) {
        enable = true;
    } else if (strncmp(buffer, "dentry_cache = off", 19) == 0) {
        enable = false;
    } else {
        pretty_logw("etc/fs: invalid dentry cache toggle");
        return -1;
    }

    dcache_set_enabled(enable);

    bool flush = false;
    if (*line2) {
        if (strncmp(line2, "flush = 1", 9) == 0) {
            flush = true;
        } else if (strncmp(line2, "flush = 0", 9) != 0) {
            pretty_logw("etc/fs: invalid flush flag");
            return -1;
        }
    }

    if (flush) {
        dcache_reset();
    }

    etc_fs_reset(inode);
    return 0;
}

static void etc_fs_daemon() {
    set_process_nice(10, current_running->pid);
    while (true) {
        inode_t* inode = path_resolve_entry("/proc/sys/fs/dentry");
        inode_open(inode);
        if (etc_fs_parse(inode) != 0) {
            pretty_logw("etc/fs: parse error, reset to default");
            etc_fs_reset(inode);
        }
        inode_close(inode);
        do_sleep(1);
    }
}

void init_fs_etc() {
    do_mkdir("/proc");
    do_mkdir("/proc/sys");
    do_mkdir("/proc/sys/fs");

    inode_t* vm_node = path_create("/proc/sys/vm", FS_TYPE_FILE);
    if (vm_node) {
        inode_open(vm_node);
        etc_vm_reset(vm_node);
        inode_close(vm_node);
    }

    inode_t* fs_node = path_create("/proc/sys/fs/dentry", FS_TYPE_FILE);
    if (fs_node) {
        inode_open(fs_node);
        etc_fs_reset(fs_node);
        inode_close(fs_node);
    }

    do_exec(NULL, "vm_conf", (uint64_t)etc_vm_daemon, 1, (char*[]){"vm_conf"}, (unsigned)-1);

    do_exec(NULL, "fs_config", (uint64_t)etc_fs_daemon, 1, (char*[]){"fs_conf"}, (unsigned)-1);
}
