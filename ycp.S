.global main

main:
    la t4, result_addr
    li t5, 1
    li t6, 200
.main_loop_cond:
    bgt t5, t6, .main_loop_end
.main_loop_body:
    mv a0, t5
    jal is_prime
    sw a0, 0(t4)
    add t4, t4, 4
    add t5, t5, 1
    j .main_loop_cond
.main_loop_end:
    j halt


is_prime:
    /*
    *  (n: a0:int) -> is_prime: a0:int
    *  corrupts: t0, t1, t2, t3
    */
    li t0, 0            # default is_prime = 0
    li t1, 1
    ble a0, t1, .prime_loop_end # if n <= 1: return 0
    li t0, 1            # is_prime = 1
    li t1, 2            # i = 2
    mv t2, a0           # n = a0
.prime_loop_cond:
    bge t1, t2, .prime_loop_end  # if i > n: break
.prime_loop_body:
    rem t3, t2, t1       # if n % i == 0:
    beqz t3, .prime_not_prime
    add t1, t1, 1
    j .prime_loop_cond
.prime_not_prime:
    li t0, 0          # is_prime = 0
.prime_loop_end:
    mv a0, t0
    jr ra


halt:
    nop
    j halt

.equ result_addr, 0x5f000000
