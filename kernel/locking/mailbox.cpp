#include <os/mbox.hpp>
extern "C" {

#include <assert.h>
#include <breakpoint.h>
#include <guard.hpp>
#include <logger.h>
#include <os/list.h>
#include <os/lock.h>
#include <os/sched.h>
#include <os/string.h>

mailbox_t mailboxes[MBOX_NUM];
spin_lock_t mbox_locks[MBOX_NUM];
pid_bitmap_t mbox_ref[MBOX_NUM] = {0};

void mailbox_init(mailbox_t* mbox) {
    memset(mbox->name, 0, sizeof(mbox->name));
    memset(mbox->buffer, 0, sizeof(mbox->buffer));
    mbox->head = 0;
    mbox->tail = 0;
    mbox->used = 0;
    mbox->nref = 0;
    mutex_init(&mbox->buffer_lock);
    condition_init(&mbox->empty);
    condition_init(&mbox->full);
}

void mailbox_destruct(mailbox_t* mbox) {
    condition_destruct(&mbox->full);
    condition_destruct(&mbox->empty);
    mutex_destruct(&mbox->buffer_lock);
    memset(mbox, 0, sizeof(mailbox_t));
}

void init_mbox() {
    for (int i = 0; i < MBOX_NUM; i++) {
        mbox_ref[i] = 0;
        spin_lock_init(&mbox_locks[i]);
        mailbox_init(&mailboxes[i]);
    }
}

void cleanup_mailboxes(pid_t pid) {
    for (int i = 0; i < MBOX_NUM; i++) {
        with_spin guard(mbox_locks[i]);
        int pcb_index = get_pcb_index(pid);
        if (mbox_ref[i] & (1ull << pcb_index)) {
            mbox_ref[i] &= ~(1ull << pcb_index);
            pretty_log(
                LOG_INFO, "released mailbox %d allocation for pid %d, remaining=0x%x", i, pid,
                mbox_ref[i]);
            if (!mbox_ref[i]) {
                pretty_log(LOG_INFO, "destroyed mailbox %d as no one is using it", i);
                mailbox_destruct(&mailboxes[i]);
            }
        }
    }
}

void list_mboxes() {
    for (int i = 0; i < MBOX_NUM; i++) {
        with_spin guard(mbox_locks[i]);
        if (mbox_ref[i]) {
            pretty_log(
                LOG_INFO, "mbox %d: name=%s, nref=%d, used=%d", i, mailboxes[i].name,
                mailboxes[i].nref, mailboxes[i].used);
        }
    }
}

int do_mbox_open(char* name) {
    list_mboxes();
    int id = -1;
    for (int i = 0; id == -1 && i < MBOX_NUM; i++) {
        with_spin guard(mbox_locks[i]);
        if (mbox_ref[i] && (strcmp(mailboxes[i].name, name) == 0)) {
            id = i;
            pretty_log(LOG_INFO, "find existing mailbox %d for name %s", id, name);
            mailboxes[id].nref++;
        }
    }
    for (int i = 0; id == -1 && i < MBOX_NUM; i++) {
        with_spin guard(mbox_locks[i]);
        if (!mbox_ref[i]) {
            mailbox_init(&mailboxes[i]);
            strcpy(mailboxes[i].name, name);
            id = i;
            pretty_log(LOG_INFO, "allocate mailbox %d for name %s", id, name);
            mailboxes[id].nref++;
        }
    }
    assert(id >= 0);
    int pcb_index = get_pcb_index(current_running->pid);
    mbox_ref[id] |= (1ull << pcb_index);
    return id;
}

void do_mbox_close(int mbox_idx) {
    list_mboxes();
    if (mbox_idx < 0 || mbox_idx >= MBOX_NUM) {
        pretty_loge("mailbox index %d out of range!", mbox_idx);
        return;
    }

    mailbox_t* mbox = &mailboxes[mbox_idx];
    with_spin guard(mbox_locks[mbox_idx]);
    if (!mbox_ref[mbox_idx]) {
        pretty_loge("mailbox %d is not initialized!", mbox_idx);
        return;
    }

    pretty_log(LOG_INFO, "closing mailbox %d", mbox_idx);
    mbox->nref--;
    if (mbox->nref > 0) {
        pretty_log(LOG_INFO, "mailbox %d still has %d references", mbox_idx, mbox->nref);
        return;
    }
    pretty_log(LOG_INFO, "destroying mailbox %d", mbox_idx);
    mbox_ref[mbox_idx] = 0;
    mailbox_destruct(mbox);
}

int do_mbox_send(int mbox_idx, suva_t msg, int msg_length) {
    if (mbox_idx < 0 || mbox_idx >= MBOX_NUM) {
        pretty_loge("mailbox index %d out of range!", mbox_idx);
        return 0;
    }

    mailbox_t* mbox = &mailboxes[mbox_idx];
    if (!mbox_ref[mbox_idx]) {
        pretty_loge("mailbox %d is not initialized!", mbox_idx);
        return 0;
    }

    int blocked = 0;
    int sent = 0;
    while (sent < msg_length) {
        with_mutex guard(mbox->buffer_lock);
        if (mbox->used < MAX_MBOX_LENGTH) {
            mbox->buffer[mbox->tail] = msg.get<char>(sent);
            mbox->tail = (mbox->tail + 1) % MAX_MBOX_LENGTH;
            mbox->used++;
            sent++;
            if (sent % 1024 == 0 || sent == msg_length)
                pretty_logd(
                    "mbox %d: sent byte %d/%d, used=%d", mbox_idx, sent, msg_length, mbox->used);
            condition_signal(&mbox->empty);
        } else {
            blocked = 1;
            pretty_logd("mbox %d: buffer full, waiting...", mbox_idx);
            condition_wait(&mbox->full, &mbox->buffer_lock);
            pretty_logd("mbox %d: woke up from wait", mbox_idx);
        }
    }
    return blocked;
}

int do_mbox_recv(int mbox_idx, suva_t msg, int msg_length) {
    if (mbox_idx < 0 || mbox_idx >= MBOX_NUM) {
        pretty_loge("mailbox index %d out of range!", mbox_idx);
        return 0;
    }

    mailbox_t* mbox = &mailboxes[mbox_idx];
    if (!mbox_ref[mbox_idx]) {
        pretty_loge("mailbox %d is not initialized!", mbox_idx);
        return 0;
    }

    int blocked = 0;
    int received = 0;
    while (received < msg_length) {
        with_mutex guard(mbox->buffer_lock);
        if (mbox->used > 0) {
            msg.set<char>(received, mbox->buffer[mbox->head]);
            mbox->head = (mbox->head + 1) % MAX_MBOX_LENGTH;
            mbox->used--;
            received++;
            if (received % 1024 == 0 || received == msg_length)
                pretty_logd(
                    "mbox %d: received byte %d/%d, used=%d", mbox_idx, received, msg_length,
                    mbox->used);
            condition_signal(&mbox->full);
        } else {
            blocked = 1;
            pretty_logd("mbox %d: buffer empty, waiting...", mbox_idx);
            condition_wait(&mbox->empty, &mbox->buffer_lock);
            pretty_logd("mbox %d: woke up from wait", mbox_idx);
        }
    }
    return blocked;
}

void show_mailboxes() {
    for (int i = 0; i < MBOX_NUM; i++) {
        with_spin guard(mbox_locks[i]);
        if (mbox_ref[i]) {
            printk(
                "mailbox %d: name=%s, ref=0x%x, nref=%d, used=%d\n", i, mailboxes[i].name,
                mbox_ref[i], mailboxes[i].nref, mailboxes[i].used);
        }
    }
}
}
