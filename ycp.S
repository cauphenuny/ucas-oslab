.global main

main:
    li t1, 1      # i = 1
    li t2, 0      # sum = 0
    li t3, 50
    j start

start:
    bgt t1, t3, end   # if i > 50: goto end
    add t2, t2, t1    # sum += i
    addi t1, t1, 1    # i += 1
    j start           # goto start

end:
    la t3, retval_addr
    sw t2, 0(t3)
    j halt

halt:
    nop
    j halt

.section data
.equ retval_addr, 0x5ffffffc
