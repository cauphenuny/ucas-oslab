#include <asm.h>

.extern __bss_start
.extern __BSS_END__
.extern sys_exit

// .equ USER_STACK_TOP, 0x5f000000

.section ".entry_function","ax"
ENTRY(_start)

    /* TODO: [p1-task3] setup C runtime environment for the user program */

    // clear bss
    la t0, __bss_start
    la t1, __BSS_END__
    mv t2, t0
.clear:
    sw zero, 0(t2)
    addi t2, t2, 4
    blt t2, t1, .clear

    /* TODO: [p1-task3] enter main function */
.enter:
    jal main


    /* TODO: [p1-task3] finish task and return to the kernel, replace this in p3-task2! */
.ret:
    j sys_exit

    /************************************************************/
    /* TODO: [P3] call sys_exit */
    /************************************************************/
// while(1) loop, unreachable here
loop:
    wfi
    j loop

END(_start)
