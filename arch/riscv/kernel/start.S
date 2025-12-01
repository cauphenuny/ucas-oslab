/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#include <asm.h>
#include <csr.h>

/* maybe you need another stack for booting? */
.equ boot_stack_top_base, 0x52000000

#define BOOT_STACK_SIZE_WIDTH 12
#define BOOT_STACK_SIZE 0x1000

.section ".bootkernel_entry","ax"
ENTRY(_boot)
  /*
   * this is the entrypoint of the kernel. 
   * Don't clear BSS here
   */

  /* Mask all interrupts */
  csrw CSR_SIE, zero
  csrw CSR_SIP, zero

  csrr a0, CSR_MHARTID 

  mv t0, a0
  add t0, t0, 1
  sll t0, t0, BOOT_STACK_SIZE_WIDTH // sp = base + (hartid + 1) * BOOT_STACK_SIZE

  la sp, boot_stack_top_base
  add sp, sp, t0

  // this will never return
  call boot_kernel


loop:
  wfi
  j loop

END(_boot)
