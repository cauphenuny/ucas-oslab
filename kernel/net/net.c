#include <e1000.h>
#include <logger.h>
#include <os/list.h>
#include <os/mm.h>
#include <os/net.h>
#include <os/sched.h>
#include <os/smp.h>
#include <os/string.h>
#include <os/time.h>
#include <type.h>

static LIST(send_block_queue, "send");
static LIST(recv_block_queue, "recv");

void net_send_wakeup() { unblock_list(&send_block_queue); }

void net_recv_wakeup() { unblock_list(&recv_block_queue); }

// Simple reliable recv stream implementation
// Header offset in packet where our protocol begins
#define RTP_HDR_OFFSET 54

static inline uint16_t be16_to_host(const uint8_t* p) { return (p[0] << 8) | p[1]; }
static inline uint32_t be32_to_host(const uint8_t* p) {
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}
static inline void host_to_be32(uint8_t* p, uint32_t v) {
    p[0] = (v >> 24) & 0xff;
    p[1] = (v >> 16) & 0xff;
    p[2] = (v >> 8) & 0xff;
    p[3] = v & 0xff;
}

typedef struct {
    uint8_t magic;
    uint8_t flags;
    uint16_t len;
    uint32_t seq;
} header_t;

static void ack(char* tx, int seq) {
    pretty_logi("acknowledge seq=%u", seq);
    uint8_t* thdr = (uint8_t*)tx + RTP_HDR_OFFSET;
    thdr[0] = 0x45;
    thdr[1] = 0x04;  // ACK
    thdr[2] = 0;
    thdr[3] = 0;  // len=0
    host_to_be32(thdr + 4, seq);
    do_net_send(tx, TX_PKT_SIZE);
}

static void rsd(char* tx, int seq) {
    pretty_logi("restore seq=%u", seq);
    uint8_t* thdr = (uint8_t*)tx + RTP_HDR_OFFSET;
    thdr[0] = 0x45;
    thdr[1] = 0x02;  // RSD
    thdr[2] = 0;
    thdr[3] = 0;  // len=0
    host_to_be32(thdr + 4, seq);
    do_net_send(tx, RX_PKT_SIZE);
}

static header_t parse(char* buffer) {
    uint8_t* hdr = (uint8_t*)buffer + RTP_HDR_OFFSET;
    header_t h;
    h.magic = hdr[0];
    h.flags = hdr[1];
    h.len = be16_to_host(hdr + 2);
    h.seq = be32_to_host(hdr + 4);
    return h;
}

static header_t poll(char* buffer, int seq) {
    int plen = 0, cnt = 0;
    while (!plen && cnt < 10) {
        plen = e1000_poll(buffer);
        cnt++;
    }
    if (!plen) {
        rsd(buffer, seq);
        do_sleep(1);
        return poll(buffer, seq);
    }
    header_t hdr = parse(buffer);
    if (hdr.magic != 0x45) {
        pretty_logd("ignore invalid pkt: expected magic=0x45, got magic=0x%x", hdr.magic);
        for (int i = 0; i < plen; i++) {
            if (buffer[i] == 0x45 && buffer[i + 1] == 0x01) {
                pretty_logd("possible valid pkt at offset %d", i);
                break;
            }
        }
    }
    if (hdr.seq != seq) {
        pretty_logd("ignore non-matching pkt: expected seq=%u, got seq=%u", seq, hdr.seq);
        return poll(buffer, seq);
    }
    return hdr;
}

static uint16_t fletcher16(uint8_t* data, int n) {
    uint16_t sum1 = 0;
    uint16_t sum2 = 0;
    int i;
    for (i = 0; i < n; ++i) {
        sum1 = (sum1 + data[i]) % 0xff;
        sum2 = (sum2 + sum1) % 0xff;
    }
    return (sum2 << 8) | sum1;
}

int do_net_recv_stream(void* buffer, int* nbytes) {
    char* out = (char*)buffer;
    int capacity = *nbytes;
    uint32_t next_seq = 0, recv_seq = 0;
    int received_total = 0;
    int want = capacity;
    char pkt[RX_PKT_SIZE];
    while (received_total < want) {
        // drain all available packets
        header_t hdr = poll(pkt, next_seq);
        char* data_start = pkt + RTP_HDR_OFFSET + 8;
        pretty_logd("received pkt: flags=0x%x, len=%d, seq=%u", hdr.flags, hdr.len, hdr.seq);

        if (hdr.flags & 0x01) {  // DAT
            if (hdr.seq == 0) {
                int size = *(uint32_t*)data_start;
                want = min(want, size);
                pretty_logd("updated size: %d", want);
            }
            // in-order
            int copy_len = hdr.len;
            if (received_total + copy_len > want) copy_len = want - received_total;
            memcpy(out + received_total, data_start, copy_len);
            received_total += copy_len;
            recv_seq = next_seq;
            next_seq += hdr.len;
            ack(pkt, recv_seq);
        }
    }
    *nbytes = received_total;
    ack(pkt, recv_seq);
    uint16_t checksum = fletcher16((uint8_t*)out + 4, received_total - 4);
    pretty_logi("total received %d bytes, fletcher16=0x%x", received_total, checksum);
    return received_total;
}

int do_net_send(void* txpacket, int length) {
    // DONE: [p5-task1] Transmit one network packet via e1000 device
    while (true) {
        int transferred = e1000_transmit(txpacket, length);
        if (transferred > 0) {
            break;
        }
        // DONE: [p5-task3] Call do_block when e1000 transmit queue is full
        do_block(&current_running->sched_node, &send_block_queue);
        // TODO: [p5-task4] Enable TXQE interrupt if transmit queue is full
        // e1000_enable_txqe();
        pretty_logd("no space in transmit queue, blocking pid %d", current_running->pid);
        do_scheduler();
        pretty_logd("pid %d woke up for sending packet", current_running->pid);
        // e1000_disable_txqe();
    }

    return length;  // Bytes it has transmitted
}

int do_net_recv(void* rxbuffer, int pkt_num, int* pkt_lens) {
    // DONE: [p5-task2] Receive one network packet via e1000 device
    int sum = 0;
    for (int i = 0; i < pkt_num; i++) {
        pkt_lens[i] = 0;
        while (true) {
            int received = e1000_poll(rxbuffer);
            if (received > 0) {
                pkt_lens[i] = received;
                rxbuffer += received;
                sum += received;
                break;
            }
            // DONE: [p5-task3] Call do_block when there is no packet on the way
            do_block(&current_running->sched_node, &recv_block_queue);
            pretty_logd("no packet received, blocking pid %d", current_running->pid);
            do_scheduler();
            pretty_logd("pid %d woke up for receiving packet", current_running->pid);
        }
    }

    return sum;  // Bytes it has received
}

void net_handle_irq(void) {
    // TODO: [p5-task4] Handle interrupts from network device
}
