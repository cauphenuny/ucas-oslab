#include <unistd.h>

int main(void)
{
    sys_set_max_memory(4096 * 64);
    sys_sleep(5);
    sys_set_max_memory(4096 * 4);
}
