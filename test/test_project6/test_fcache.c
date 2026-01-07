#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUF_SIZE 256
#define DEFAULT_OPS 20000
#define DEFAULT_FILE_KB 512
#define CONFIG_SETTLE_SEC 2

static char buffer[BUF_SIZE];

static inline uint64_t now_ticks(void) { return (uint64_t)sys_get_proc_tick(); }

static void apply_vm_policy(const char* policy, int write_back_freq) {
    int fd = sys_open("/proc/sys/vm", O_WRONLY);
    if (fd < 0) {
        printf("open /proc/sys/vm failed (%d)\n", fd);
        sys_exit();
    }
    char data[128];
    int len = snprintf(
        data, sizeof(data), "page_cache_policy = %s\nwrite_back_freq = %d\n", policy,
        write_back_freq);
    sys_lseek(fd, 0, SEEK_SET);
    sys_write(fd, data, len);
    sys_close(fd);
    sys_sleep(CONFIG_SETTLE_SEC);
}

static uint64_t run_small_write_workload(int ops, int file_kb) {
    int fd = sys_open("stress.bin", O_RDWR);
    if (fd < 0) {
        printf("open stress.bin failed (%d)\n", fd);
        sys_exit();
    }

    int target_bytes = file_kb * 1024;
    for (int written = 0; written < target_bytes; written += BUF_SIZE) {
        int ret = sys_write(fd, buffer, BUF_SIZE);
        if (ret <= 0) {
            printf("prefill write failed (%d)\n", ret);
            sys_exit();
        }
    }

    sys_lseek(fd, 0, SEEK_SET);

    uint64_t start = now_ticks();
    int max_offset = target_bytes > BUF_SIZE ? target_bytes - BUF_SIZE : 0;
    for (int i = 0; i < ops; i++) {
        int offset = max_offset ? (rand() % (max_offset + 1)) : 0;
        sys_lseek(fd, offset, SEEK_SET);
        int ret = sys_write(fd, buffer, BUF_SIZE);
        if (ret <= 0) {
            printf("random write failed (%d)\n", ret);
            sys_exit();
        }
    }
    uint64_t duration = now_ticks() - start;

    sys_close(fd);
    return duration;
}

static void cleanup_artifacts(void) {
    sys_rm("stress.bin");
}

int main(int argc, char** argv) {
    int ops = DEFAULT_OPS;
    int file_kb = DEFAULT_FILE_KB;

    if (argc >= 2) {
        ops = atoi(argv[1]);
    }
    if (argc >= 3) {
        file_kb = atoi(argv[2]);
    }

    if (ops <= 0) ops = DEFAULT_OPS;
    if (file_kb <= 0) file_kb = DEFAULT_FILE_KB;

    for (int i = 0; i < BUF_SIZE; i++) {
        buffer[i] = (char)(i & 0xff);
    }

    cleanup_artifacts();

    printf("== small random write benchmark ==\n");
    printf("ops = %d, file = %d KB\n", ops, file_kb);

    apply_vm_policy("write through", 1);
    uint64_t wt_ticks = run_small_write_workload(ops, file_kb);
    printf("write-through duration: %lu ticks\n", (unsigned long)wt_ticks);

    cleanup_artifacts();

    apply_vm_policy("write back", 300);
    uint64_t wb_ticks = run_small_write_workload(ops, file_kb);
    printf("write-back duration:   %lu ticks\n", (unsigned long)wb_ticks);

    cleanup_artifacts();

    printf("done.\n");
    return 0;
}
