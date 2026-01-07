#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_FILES         6000
#define CONFIG_SETTLE_SEC 2

static int order[MAX_FILES];

static inline uint64_t now_ticks(void) { return (uint64_t)sys_get_proc_tick(); }

static void write_dcache_config(int enable, int flush) {
    char path[] = "/proc/sys/fs/dentry";
    int fd = sys_open(path, O_RDWR);
    if (fd < 0) {
        printf("failed to open %s (err=%d)\n", path, fd);
        sys_exit();
    }

    char buffer[64];
    int len = snprintf(
        buffer, sizeof(buffer), "dentry_cache = %s\nflush = %d\n", enable ? "on" : "off", flush);
    sys_lseek(fd, 0, SEEK_SET);
    sys_write(fd, buffer, len);
    sys_close(fd);
}

static void apply_dcache_config(int enable, int flush) {
    write_dcache_config(enable, flush);
    sys_sleep(CONFIG_SETTLE_SEC);
}

static void shuffle_order(int count) {
    for (int i = 0; i < count; i++) {
        order[i] = i;
    }
    for (int i = count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
    }
}

static uint64_t populate_files(int count) {
    char name[32];
    uint64_t start = now_ticks();
    for (int i = 0; i < count; i++) {
        snprintf(name, sizeof(name), "file_%04d", i);
        int fd = sys_open(name, O_RDWR);
        if (fd < 0) {
            printf("create %s failed (%d)\n", name, fd);
            sys_exit();
        }
        sys_close(fd);
    }
    return now_ticks() - start;
}

static uint64_t random_open_pass(int count, int rounds) {
    char name[32];
    uint64_t start = now_ticks();
    for (int r = 0; r < rounds; r++) {
        shuffle_order(count);
        for (int i = 0; i < count; i++) {
            snprintf(name, sizeof(name), "file_%04d", order[i]);
            int fd = sys_open(name, O_RDONLY);
            if (fd < 0) {
                printf("open %s failed (%d)\n", name, fd);
                sys_exit();
            }
            sys_close(fd);
        }
    }
    return now_ticks() - start;
}

static void cleanup_files(int count) {
    char name[32];
    for (int i = 0; i < count; i++) {
        snprintf(name, sizeof(name), "file_%04d", i);
        sys_rm(name);
    }
}

int main(int argc, char** argv) {
    int file_count = 500;
    int rounds = 2;

    if (argc >= 2) {
        file_count = atoi(argv[1]);
    }
    if (argc >= 3) {
        rounds = atoi(argv[2]);
    }

    if (file_count > MAX_FILES) {
        printf("file count capped at %d\n", MAX_FILES);
        file_count = MAX_FILES;
    }
    if (file_count <= 0) file_count = 1;
    if (rounds <= 0) rounds = 1;

    uint64_t seed = now_ticks();
    srand((uint32_t)(seed & 0xffffffffu));

    printf("== dentry cache benchmark ==\n");
    printf("files = %d, rounds = %d\n", file_count, rounds);

    sys_cd("/");
    char bench_dir[32];
    snprintf(bench_dir, sizeof(bench_dir), "bench_dcache_%04lu", (unsigned long)(seed & 0xffff));
    sys_mkdir(bench_dir);
    sys_cd(bench_dir);
    printf("working directory: %s\n", bench_dir);

    uint64_t create_ticks = populate_files(file_count);
    printf("create phase: %lu ticks\n", (unsigned long)create_ticks);

    apply_dcache_config(0, 1);
    uint64_t off_ticks = random_open_pass(file_count, rounds);
    printf("cache off random open: %lu ticks\n", (unsigned long)off_ticks);

    apply_dcache_config(1, 1);
    uint64_t on_cold = random_open_pass(file_count, rounds);
    uint64_t on_warm = random_open_pass(file_count, rounds);
    printf("cache on cold run:  %lu ticks\n", (unsigned long)on_cold);
    printf("cache on warm run:  %lu ticks\n", (unsigned long)on_warm);

    cleanup_files(file_count);
    sys_cd("/");
    sys_rmdir(bench_dir);

    apply_dcache_config(1, 0);

    printf("benchmark complete.\n");
    return 0;
}
