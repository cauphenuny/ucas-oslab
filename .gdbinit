set confirm off
set architecture riscv:rv64
set disassemble-next-line auto
# breakpoint flag
watch *0x5ffffffc

break switch_to if *(int*)1380982748 == 23
set breakpoint pending on
