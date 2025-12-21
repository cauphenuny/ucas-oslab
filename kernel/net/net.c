#include <e1000.h>
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
    // TODO: [p5-task1] Transmit one network packet via e1000 device
    while (true) {
        int transferred = e1000_transmit(txpacket, length);
        if (transferred > 0) {
            break;
        }
        do_block(&current_running->sched_node, &send_block_queue);
        do_scheduler();
    }
    // TODO: [p5-task3] Call do_block when e1000 transmit queue is full
    // TODO: [p5-task4] Enable TXQE interrupt if transmit queue is full

    return length;  // Bytes it has transmitted
}

int do_net_recv(void* rxbuffer, int pkt_num, int* pkt_lens) {
    // TODO: [p5-task2] Receive one network packet via e1000 device
    // TODO: [p5-task3] Call do_block when there is no packet on the way

    return 0;  // Bytes it has received
}

void net_handle_irq(void) {
    // TODO: [p5-task4] Handle interrupts from network device
}
