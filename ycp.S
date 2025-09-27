.global main

main:
    li a0, 24
    jal is_prime
    j halt


is_prime:
    /*
    *  (n: a0:int) -> is_prime: a0:int
    */
    li t0, 1          # is_prime = 1
    li t1, 2          # i = 2
    mv t2, a0         # n = a0
loop_cond:
    bge t1, t2, loop_end  # if i > n: break
loop_body:
    rem t3, t2, t1       # if n % i == 0:
    beqz t3, not_prime
    add t1, t1, 1
    j loop_cond
not_prime:
    li t0, 0          # is_prime = 0
loop_end:
    mv a0, t0
    jr ra


halt:
    nop
    j halt
