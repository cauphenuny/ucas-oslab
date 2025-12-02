/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#include <asm.h>
#include <csr.h>

/* maybe you need another stack for booting? */
.equ boot_stack_top_base, 0x51000000

.equ BOOT_STACK_SIZE_WIDTH, 12 # 4096 bytes stack size

.section ".bootkernel_entry","ax"
ENTRY(_boot)
  /*
   * this is the entrypoint of the kernel. 
   * Don't clear BSS here
   */

  /* Mask all interrupts */
  csrw CSR_SIE, zero
  csrw CSR_SIP, zero

  // (a0: argc, a1: argv) => (a1: argc, a2: argv)
  mv a2, a1
  mv a1, a0

  // a0: hartid
  csrr a0, CSR_MHARTID
  mv t0, a0
  sll t0, t0, BOOT_STACK_SIZE_WIDTH // sp = base - (hartid) * BOOT_STACK_SIZE
  la sp, boot_stack_top_base
  sub sp, sp, t0

  // this will never return
  call boot_kernel


loop:
  wfi
  j loop

END(_boot)
