#include <e1000.h>
#include <logger.h>
#include <os/list.h>
#include <os/sched.h>
#include <os/smp.h>
#include <os/string.h>
#include <type.h>

static LIST(send_block_queue, "send");
static LIST(recv_block_queue, "recv");

void net_send_wakeup() { unblock_list(&send_block_queue); }

void net_recv_wakeup() { unblock_list(&recv_block_queue); }

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
