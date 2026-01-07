#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static char buff[128], rbuf[128];

void cacheconf(const char* policy, int write_back_freq) {
    int fd = sys_open("/proc/sys/vm", O_WRONLY);
    char buffer[1024];
    int size = snprintf(
        buffer, 1024, "page_cache_policy = %s\nwrite_back_freq = %d\n", policy, write_back_freq);
    sys_write(fd, buffer, size);
    sys_close(fd);
}

uint64_t bench(int size, int repeat) {
    uint64_t start = sys_get_proc_tick();
    int fd = sys_open("large.txt", O_RDWR);
    for (int t = 0; t < repeat; t++) {
        for (int k = 0; k < size; k++) {
            // write 1M
            for (int i = 0; i < 8 * 1024; i++) {
                sys_write(fd, buff, 128);
            }
        }
        for (int k = 0; k < size; k++) {
            // read 1M
            for (int i = 0; i < 8 * 1024; i++) {
                sys_read(fd, buff, 128);
                for (int i = 0; i < 128; i++) {
                    if (buff[i] != i) {
                        printf("data mismatch at byte %d: expected %d, got %d\n", i, i, buff[i]);
                        sys_exit();
                    }
                }
            }
        }
    }
    sys_close(fd);
    return sys_get_proc_tick() - start;
}

int main(int argc, char** argv) {
    int size = 1, repeat = 2;
    if (argc >= 2) {
        size = atoi(argv[1]);
    }
    if (argc >= 3) {
        repeat = atoi(argv[2]);
    }
    printf("Benchmarking cache with size=%d MB, repeat=%d times\n", size, repeat);

    for (int i = 0; i < 128; i++) {
        buff[i] = i;
    }

    cacheconf("write_back", 60);

    uint64_t time_wb = bench(size, repeat);
    printf("Write-back cache time: %d ticks\n", time_wb);

    cacheconf("write_through", 1);
    uint64_t time_wt = bench(size, repeat);
    printf("Write-through cache time: %d ticks\n", time_wt);

    return 0;
}
