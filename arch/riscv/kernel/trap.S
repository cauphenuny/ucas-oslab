#include <asm.h>
#include <csr.h>

.extern exception_handler_entry

ENTRY(setup_exception)

  /* DONE: [p2-task3] save exception_handler_entry into STVEC */

  la t0, exception_handler_entry # 00-aligned
  csrw stvec, t0

  /* DONE: [p2-task4] enable interrupts globally */

  li   t0, SR_SPIE
  csrs sstatus, t0

  jr ra

ENDPROC(setup_exception)
