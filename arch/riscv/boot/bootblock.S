#include <asm/biosdef.h>

// os size location (os_size could be sectors [p1-task3] or bytes [p1-task4])
.equ os_size_loc, 0x502001fc
.equ task_num_loc, 0x502001fe

// kernel address (move kernel to here, and jmp here to start kernel)
.equ kernel, 0x50201000

// BIOS function entry (jump here to use BIOS APIs)
.equ bios_func_entry, 0x50150000

.equ bios_putstr, 9
.equ bios_sdread, 11

.text
.global main

main:
	// fence on all memory and I/O
	fence

	// TODO: [p1-task1] call BIOS to print string "It's bootblock!"
	la a7, bios_putstr
	la a0, msg
	jal bios_func_entry


	// TODO: [p1-task2] call BIOS to read kernel in SD card
	// a0: mem_addr
	// a1: num_of_blocks
	// a2: block_id
	// a7: bios_sdread
	la a0, kernel
	la t0, os_size_loc
	lh a1, 0(t0)
	li a2, 1
	la a7, bios_sdread
	jal bios_func_entry


	// TODO: [p1-task4] load task-related arguments and pass them to kernel


	// TODO: [p1-task2] jump to kernel to start UCAS-OS
	la t0, task_num_loc
	lh a0, 0(t0)
	j kernel

	/************************************************************/
	/* Do not touch this comment. Reserved for future projects. */
	/************************************************************/
// while(1) --> stop here
stop:
	j stop
	nop

.data

msg: .string "It's ycp's bootloader...\n\r"

