#include <asm/biosdef.h>
#include <csr.h>

// os size location (os_size could be sectors [p1-task3] or bytes [p1-task4])
.equ os_size_loc,	0xffffffc0502001fa
.equ task_start_loc,	0xffffffc0502001f8
.equ task_size_loc,	0xffffffc0502001f6
.equ task_num_loc,	0xffffffc0502001f4
.equ batchfile_loc,	0xffffffc0502001f2

// kernel address (move kernel to here, and jmp here to start kernel)
.equ kernel, 0xffffffc050202000

// BIOS function entry (jump here to use BIOS APIs)
.equ bios_func_entry, 0xffffffc050150000

.equ bios_putstr, 9
.equ bios_sdread, 11

.extern tasks

.text
.global main

main:
    // fence on all memory and I/O
    fence
    // a0 is mhartid
    bnez a0, secondary

.hello_world:
    // TODO: [p1-task1] call BIOS to print string "It's bootblock!"
    la a7, bios_putstr
    la a0, msg
    jal bios_func_entry


    // TODO: [p1-task2] call BIOS to read kernel in SD card

.load_kernel:

    add sp, sp, -512 // make space for one sector
    // NOTE: since we don't return from bbl, so we can use s registers freely
    li s0, 1 // s0: current sector_id

    la t0, os_size_loc
    lw a0, 0(t0) // kernel bytes
    jal calculate_sectors

    mv s1, a0 // s1: remaining sectors

    la s2, kernel // s2: current kenrel_addr
    mv s3, sp // s3: temp buffer addr

.load_kernel_loop_body:

    // read data
    mv a0, s3 // a0: memory_addr
    li a1, 1  // a1: num_of_blocks
    mv a2, s0 // a2: block_id
    la a7, bios_sdread // a7: bios_sdread
    jal bios_func_entry


    // copy data
    la a0, s2
    mv a1, sp
    li a2, 512
    jal copy_data

    // update counters
    add s0, s0, 1 // next sector
    add s1, s1, -1 // remaining sectors
    add s2, s2, 512 // next kernel addr
    bnez s1, .load_kernel_loop_body

.load_kenrel_loop_end:
    add sp, sp, 512


    // TODO: [p1-task4] load task-related arguments and pass them to kernel
.load_taskinfo:
    la t0, task_size_loc
    lh a0, 0(t0) // tasks bytes
    jal calculate_sectors
    mv a1, a0 // a1: num_of_sectors

    mv t0, a0
    sll t0, t0, 9 // t0: aligned_nbytes = num_of_sectors * 512
    sub sp, sp, t0 // make space for taskinfo

    la t0, task_start_loc
    lh a2, 0(t0) // a2: tasks start block_id

    la a0, sp // a0: memory_addr
    la a7, bios_sdread // a7: bios_sdread
    jal bios_func_entry


    // TODO: [p1-task2] jump to kernel to start UCAS-OS
.start_kernel:
    mv s0, sp # taskinfo location
    add sp, sp, -24 # kernel args: task_num, taskinfo, batchfile

    la t0, task_num_loc
    lh t0, 0(t0)
    sd t0, 0(sp) // argv+0: task_num
    sd s0, 8(sp) // argv+8: taskinfo
    la t0, batchfile_loc
    lh t0, 0(t0)
    sd t0, 16(sp) // argv+16: batchfile
    li a0, 3 // argc
    mv a1, sp // argv
    la ra, stop
    j kernel

	/************************************************************/
secondary:
	/* TODO [P3-task3]: 
	 * 1. Mask all interrupts
	 * 2. let stvec pointer to kernel_main
	 * 3. enable software interrupt for ipi
	 */
    csrw sie, zero // mask all interrupts
    la t0, kernel
    csrw stvec, t0
    csrw sie, SIE_SSIE
    li t0, SR_SIE
    csrs sstatus, t0
    j wait_for_wakeup

wait_for_wakeup:
    wfi
    j wait_for_wakeup

	/************************************************************/
// while(1) --> stop here
stop:
    j stop
    nop

copy_data:
    // a0: dest
    // a1: src
    // a2: nbytes
.copy_start:
    mv t0, a0 // t0: dest_cur
    la t1, a1 // t1: src_cur
    la t2, a1
    add t2, t2, a2 // t2: src_end
.copy_loop:
    lw t3, 0(t1)
    sw t3, 0(t0)
    add t0, t0, 4
    add t1, t1, 4
    blt t1, t2, .copy_loop
.copy_end:
    ret

calculate_sectors:
    // a0: nbytes
    // returns a0: ceiled sectors
    add a0, a0, 511
    sra a0, a0, 9 // sectors = ((bytes + 511) / 512)
    ret

.data

msg: .string "It's ycp's bootloader...\n\r"

