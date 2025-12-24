#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

uint8_t buffer[64 * 1024];

uint16_t fletcher16(uint8_t* data, int n) {
    uint16_t sum1 = 0;
    uint16_t sum2 = 0;
    int i;
    for (i = 0; i < n; ++i) {
        sum1 = (sum1 + data[i]) % 0xff;
        sum2 = (sum2 + sum1) % 0xff;
    }
    return (sum2 << 8) | sum1;
}

int main(int argc, char** argv) {
    int nbytes = sizeof(buffer);
    int r = sys_net_recv_stream(buffer, &nbytes);
    if (r <= 0 || nbytes <= 0) {
        return 1;
    }

    uint16_t checksum = fletcher16(buffer, nbytes);
    printf("recvs: size = %d, fletcher16 = 0x%x\n", nbytes, checksum);
    buffer[160] = 0;
    printf("%s", buffer);
    return 0;
}
