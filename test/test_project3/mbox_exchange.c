#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread.h>
#include <time.h>
#include <unistd.h>

int mbox1, mbox2;

typedef struct {
    int mutex;
    int sem;
    char buf[8];
} proc_data_t;

proc_data_t p0_data, p1_data;

int proc0_thread0(int argc, char** argv) {
    int cnt = 0;
    char local_buf[8];
    while (1) {
        sys_semaphore_up(p0_data.sem);
        sys_mbox_recv(mbox1, local_buf, 8);
        sys_move_cursor(0, 0);
        printf("%s: recved %d msgs from mbox1\n", argv[0], ++cnt);
        sys_mutex_acquire(p0_data.mutex);
        memcpy(p0_data.buf, local_buf, 8);
        sys_mutex_release(p0_data.mutex);
    }
    return 0;
}

int proc0_thread1(int argc, char** argv) {
    int cnt = 0;
    char local_buf[8];
    while (1) {
        sys_semaphore_down(p0_data.sem);
        sys_mbox_send(mbox2, local_buf, 8);
        sys_move_cursor(0, 1);
        printf("%s: sent %d msgs to mbox2\n", argv[0], ++cnt);
        sys_mutex_acquire(p0_data.mutex);
        memcpy(local_buf, p0_data.buf, 8);
        sys_mutex_release(p0_data.mutex);
    }
    return 0;
}

int proc0(int argc, char** argv) {
    p0_data.sem = sys_semaphore_init(0, 0);
    p0_data.mutex = sys_mutex_init(0);
    thread_t t0, t1;
    thread_create(&t0, proc0_thread0, 1, (char*[]){"p0t0"});
    thread_create(&t1, proc0_thread1, 1, (char*[]){"p0t1"});
    thread_join(t0);
    thread_join(t1);
    return 0;
}

int proc1_thread0(int argc, char** argv) {
    int cnt = 0;
    char local_buf[8];
    while (1) {
        sys_semaphore_up(p1_data.sem);
        sys_mbox_recv(mbox2, local_buf, 8);
        sys_move_cursor(0, 2);
        printf("%s: recved %d msgs from mbox2\n", argv[0], ++cnt);
        sys_mutex_acquire(p1_data.mutex);
        memcpy(p1_data.buf, local_buf, 8);
        sys_mutex_release(p1_data.mutex);
    }
    return 0;
}

int proc1_thread1(int argc, char** argv) {
    int cnt = 0;
    char local_buf[8];
    while (1) {
        sys_semaphore_down(p1_data.sem);
        sys_mbox_send(mbox1, local_buf, 8);
        sys_move_cursor(0, 3);
        printf("%s: sent %d msgs to mbox1\n", argv[0], ++cnt);
        sys_mutex_acquire(p1_data.mutex);
        memcpy(local_buf, p1_data.buf, 8);
        sys_mutex_release(p1_data.mutex);
    }
    return 0;
}

int proc1(int argc, char** argv) {
    p1_data.sem = sys_semaphore_init(1, 0);
    p1_data.mutex = sys_mutex_init(1);
    thread_t t0, t1;
    thread_create(&t0, proc1_thread0, 1, (char*[]){"p1t0"});
    thread_create(&t1, proc1_thread1, 1, (char*[]){"p1t1"});
    thread_join(t0);
    thread_join(t1);
    return 0;
}

int main() {
    mbox1 = sys_mbox_open("mbox1");
    mbox2 = sys_mbox_open("mbox2");
    for (int i = 0; i < 7; i++) {
        sys_mbox_send(mbox1, "12345678", 8);
        sys_mbox_send(mbox2, "12345678", 8);
    }
    thread_t p0, p1;
    thread_create(&p0, proc0, 1, (char*[]){"proc0"});
    thread_create(&p1, proc1, 1, (char*[]){"proc1"});
    thread_join(p0);
    thread_join(p1);
    return 0;
}
