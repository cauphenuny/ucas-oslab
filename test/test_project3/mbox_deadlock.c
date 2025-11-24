#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread.h>
#include <time.h>
#include <unistd.h>

int mbox1, mbox2;

int proc0(int argc, char** argv) {
    char msg[4];
    int cnt = 0;
    while (1) {
        sys_mbox_send(mbox1, "abcdefgh", 4);
        sys_move_cursor(0, 0);
        cnt++;
        printf("%s: sent %d msgs to mbox1\n", argv[0], cnt);
        sys_mbox_recv(mbox2, msg, 4);
    }
}

int proc1(int argc, char** argv) {
    char msg[4];
    int cnt = 0;
    while (1) {
        sys_mbox_send(mbox2, "abcdefgh", 8);
        sys_move_cursor(0, 1);
        cnt++;
        printf("%s: sent %d msgs to mbox2\n", argv[0], cnt);
        sys_mbox_recv(mbox1, msg, 4);
    }
}

int main() {
    mbox1 = sys_mbox_open("mbox1");
    mbox2 = sys_mbox_open("mbox2");
    for (int i = 0; i < 7; i++) {
        sys_mbox_send(mbox1, "12345678", 8);
        sys_mbox_send(mbox2, "12345678", 8);
    }
    thread_t t0, t1;
    thread_create(&t0, proc0, 1, (char*[]){"thread0"});
    thread_create(&t1, proc1, 1, (char*[]){"thread1"});
    thread_join(t0);
    thread_join(t1);
    return 0;
}
