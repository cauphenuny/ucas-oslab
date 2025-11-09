#include <asm.h>
#include <csr.h>

.extern exception_handler_entry

ENTRY(setup_exception)

  /* TODO: [p2-task3] save exception_handler_entry into STVEC */

  la t0, exception_handler_entry # 00-aligned
  csrw stvec, t0
  jr ra

  /* TODO: [p2-task4] enable interrupts globally */

ENDPROC(setup_exception)
