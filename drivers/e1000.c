#include <assert.h>
#include <e1000.h>
#include <logger.h>
#include <os/net.h>
#include <os/string.h>
#include <os/time.h>
#include <pgtable.h>
#include <type.h>

// E1000 Registers Base Pointer
volatile uint8_t* e1000;  // use virtual memory address

// E1000 Tx & Rx Descriptors
static struct e1000_tx_desc tx_desc_array[TXDESCS] __attribute__((aligned(16)));
static struct e1000_rx_desc rx_desc_array[RXDESCS] __attribute__((aligned(16)));

// E1000 Tx & Rx packet buffer
static char tx_pkt_buffer[TXDESCS][TX_PKT_SIZE];
static char rx_pkt_buffer[RXDESCS][RX_PKT_SIZE];

// Fixed Ethernet MAC Address of E1000
static const uint8_t enetaddr[6] = {0x00, 0x0a, 0x35, 0x00, 0x1e, 0x53};

/**
 * e1000_reset - Reset Tx and Rx Units; mask and clear all interrupts.
 **/
static void e1000_reset(void) {
    /* Turn off the ethernet interface */
    e1000_write_reg(e1000, E1000_RCTL, 0);
    e1000_write_reg(e1000, E1000_TCTL, 0);

    /* Clear the transmit ring */
    e1000_write_reg(e1000, E1000_TDH, 0);
    e1000_write_reg(e1000, E1000_TDT, 0);

    /* Clear the receive ring */
    e1000_write_reg(e1000, E1000_RDH, 0);
    e1000_write_reg(e1000, E1000_RDT, 0);

    /**
     * Delay to allow any outstanding PCI transactions to complete before
     * resetting the device
     */
    latency(1);

    /* Clear interrupt mask to stop board from generating interrupts */
    e1000_write_reg(e1000, E1000_IMC, 0xffffffff);

    /* Clear any pending interrupt events. */
    while (0 != e1000_read_reg(e1000, E1000_ICR));
}

/**
 * e1000_configure_tx - Configure 8254x Transmit Unit after Reset
 **/
static void e1000_configure_tx(void) {
    /* DONE: [p5-task1] Initialize tx descriptors */
    for (int i = 0; i < TXDESCS; i++) {
        tx_desc_array[i].addr = kva2pa((uintptr_t)tx_pkt_buffer[i]);
        tx_desc_array[i].length = TX_PKT_SIZE;
        tx_desc_array[i].status = 0;
    }

    /* DONE: [p5-task1] Set up the Tx descriptor base address and length */
    uintptr_t mask = (1ul << 32) - 1;
    uintptr_t physaddr = kva2pa((kva_t)tx_desc_array);
    e1000_write_reg(e1000, E1000_TDBAL, physaddr & mask);
    e1000_write_reg(e1000, E1000_TDBAH, (physaddr >> 32) & mask);
    e1000_write_reg(e1000, E1000_TDLEN, sizeof(tx_desc_array));

    /* DONE: [p5-task1] Set up the HW Tx Head and Tail descriptor pointers */
    e1000_write_reg(e1000, E1000_TDH, 0);
    e1000_write_reg(e1000, E1000_TDT, 0);

    /* DONE: [p5-task1] Program the Transmit Control Register */
    uint64_t tctl = E1000_TCTL_EN | E1000_TCTL_PSP;
    tctl |= FIELD_PREP(E1000_TCTL_CT, 0x10);    // collision threshold
    tctl |= FIELD_PREP(E1000_TCTL_COLD, 0x40);  // collision distance
    e1000_write_reg(e1000, E1000_TCTL, tctl);
}

/**
 * e1000_configure_rx - Configure 8254x Receive Unit after Reset
 **/
static void e1000_configure_rx(void) {
    /* DONE: [p5-task2] Set e1000 MAC Address to RAR[0] */

    uint32_t ral0 = E1000_RA;
    uint32_t rah0 = E1000_RA + 4;
    e1000_write_reg(
        e1000, ral0, enetaddr[0] | (enetaddr[1] << 8) | (enetaddr[2] << 16) | (enetaddr[3] << 24));
    e1000_write_reg(e1000, rah0, enetaddr[4] | (enetaddr[5] << 8) | E1000_RAH_AV);

    /* DONE: [p5-task2] Initialize rx descriptors */
    for (int i = 0; i < TXDESCS; i++) {
        rx_desc_array[i].addr = kva2pa((uintptr_t)rx_pkt_buffer[i]);
        rx_desc_array[i].status = 0;
    }

    /* DONE: [p5-task2] Set up the Rx descriptor base address and length */
    uintptr_t mask = (1ul << 32) - 1;
    pa_t addr = kva2pa((kva_t)rx_desc_array);
    e1000_write_reg(e1000, E1000_RDBAL, addr & mask);
    e1000_write_reg(e1000, E1000_RDBAH, (addr >> 32) & mask);
    e1000_write_reg(e1000, E1000_RDLEN, sizeof(rx_desc_array));

    /* DONE: [p5-task2] Set up the HW Rx Head and Tail descriptor pointers */
    e1000_write_reg(e1000, E1000_RDH, 0);
    e1000_write_reg(e1000, E1000_RDT, RXDESCS - 1);

    /* DONE: [p5-task2] Program the Receive Control Register */
    uint32_t rctl = E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SZ_2048 | (E1000_RCTL_BSEX & 0) |
                    E1000_RCTL_RDMTS_HALF;
    e1000_write_reg(e1000, E1000_RCTL, rctl);

    /* DONE: [p5-task4] Enable TXQE / RXDMT0 Interrupt */
    e1000_write_reg(e1000, E1000_IMS, E1000_IMS_RXDMT0 | E1000_IMS_TXQE);
    e1000_write_reg(e1000, E1000_IMC, ~(E1000_IMC_TXQE | E1000_IMC_RXDMT0));
}

/**
 * e1000_init - Initialize e1000 device and descriptors
 **/
void e1000_init(void) {
    /* Reset E1000 Tx & Rx Units; mask & clear all interrupts */
    e1000_reset();

    /* Configure E1000 Tx Unit */
    e1000_configure_tx();

    /* Configure E1000 Rx Unit */
    e1000_configure_rx();
}

/**
 * e1000_transmit - Transmit packet through e1000 net device
 * @param txpacket - The buffer address of packet to be transmitted
 * @param length - Length of this packet
 * @return - Number of bytes that are transmitted successfully
 **/
int e1000_transmit(void* txpacket, int length) {
    /* DONE: [p5-task1] Transmit one packet from txpacket */
    asserts(length <= TX_PKT_SIZE, "length exceeds maximum");

    static int tail = 0;

    int head = e1000_read_reg(e1000, E1000_TDH);
    int next = (tail + 1) % TXDESCS;
    if (head == next) {
        pretty_logw("transmit queue full, head: %d, tail: %d", head, tail);
        return 0;
    }

    memcpy((void*)tx_pkt_buffer[tail], txpacket, length);
    tx_desc_array[tail].length = length;
    tx_desc_array[tail].status = 0;
    tx_desc_array[tail].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    local_flush_dcache();

    tail = next;
    e1000_write_reg(e1000, E1000_TDT, tail);

    pretty_logd("packet transmitted, length=%d", length);
    return length;
}

/**
 * e1000_poll - Receive packet through e1000 net device
 * @param rxbuffer - The address of buffer to store received packet
 * @return - Length of received packet
 **/
int e1000_poll(void* rxbuffer) {
    /* DONE: [p5-task2] Receive one packet and put it into rxbuffer */

    static int head = 0;

    local_flush_dcache();
    if (!rx_desc_array[head].status) {
        // pretty_logw("no packet received, head: %d", head);
        return 0;
    }
    rmb();
    size_t length = rx_desc_array[head].length;

    memcpy(rxbuffer, (void*)rx_pkt_buffer[head], length);
    rx_desc_array[head].status = 0;

    pretty_logd("packet received, length=%d", length);

    // free head descriptor
    wmb();
    e1000_write_reg(e1000, E1000_RDT, head);
    head = (head + 1) % RXDESCS;

    return length;
}

void e1000_enable_txqe() {
    uint32_t imc = e1000_read_reg(e1000, E1000_IMC);
    imc = imc & (~E1000_IMC_TXQE);
    e1000_write_reg(e1000, E1000_IMC, imc);
}

void e1000_disable_txqe() {
    uint32_t imc = e1000_read_reg(e1000, E1000_IMC);
    imc = imc | E1000_IMC_TXQE;
    e1000_write_reg(e1000, E1000_IMC, imc);
}

/**
 * e1000_handle_txqe - Handle TX Queue Empty Interrupt
 **/
void e1000_handle_txqe() { net_send_wakeup(); }

/**
 * e1000_handle_rxdmt0 - Handle RX Desc Min. Threshold Interrupt
 **/
void e1000_handle_rxdmt0() { net_recv_wakeup(); }

/**
 * e1000_handle_interrupt - Handle e1000 interrupt
 **/
void e1000_handle_interrupt(void) {
    uint32_t icr = e1000_read_reg(e1000, E1000_ICR);
    if (icr & E1000_ICR_RXDMT0) {
        e1000_handle_rxdmt0();
        icr &= ~E1000_ICR_RXDMT0;
    }
    if (icr & E1000_ICR_TXQE) {
        e1000_handle_txqe();
        icr &= ~E1000_ICR_TXQE;
    }
    if (icr) {
        pretty_logw("unknown e1000 interrupt, icr=%x", icr);
    }
}
