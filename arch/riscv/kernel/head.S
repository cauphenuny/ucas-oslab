/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#include <asm.h>
#include <csr.h>

#define KERNEL_STACK		0x50500000

.equ page_size, 4096

.extern __bss_start
.extern __BSS_END__

.section ".entry_function","ax"
ENTRY(_start)
  /* Mask all interrupts */
  csrw CSR_SIE, zero
  csrw CSR_SIP, zero

  csrr t0, mhartid
  bnez t0, .stack // skip bss clearing when hartid != 0

  /* TODO: [p1-task2] clear BSS for flat non-ELF images */

  la t0, __bss_start
  la t1, __BSS_END__
  mv t2, t0

.clear:
  sw zero, 0(t2)
  addi t2, t2, 4
  blt t2, t1, .clear

  /* TODO: [p1-task2] setup C environment */

.stack:
  li t0, KERNEL_STACK
  csrr t1, mhartid
  addi t1, t1, 1
  la t2, page_size
  mul t3, t1, t2
  add t0, t0, t3
  mv sp, t0 // sp = KERNEL_STACK + (hartid + 1) * page_size

.main:
  jal main


loop:
  wfi
  j loop

END(_start)
