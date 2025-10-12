```
$ riscv64-unknown-linux-gnu-objdump -d ycp

ycp:     file format elf64-littleriscv


Disassembly of section .text:

0000000050000000 <_ftext>:
    50000000:   0f000e97                auipc   t4,0xf000
    50000004:   000e8e93                mv      t4,t4
    50000008:   4f05                    li      t5,1
    5000000a:   0c800f93                li      t6,200

000000005000000e <.main_loop_cond>:
    5000000e:   01efca63                blt     t6,t5,50000022 <.main_loop_end>

0000000050000012 <.main_loop_body>:
    50000012:   857a                    mv      a0,t5
    50000014:   010000ef                jal     50000024 <is_prime>
    50000018:   00aea023                sw      a0,0(t4) # 5f000000 <result_addr>
    5000001c:   0e91                    addi    t4,t4,4
    5000001e:   0f05                    addi    t5,t5,1
    50000020:   b7fd                    j       5000000e <.main_loop_cond>

0000000050000022 <.main_loop_end>:
    50000022:   a01d                    j       50000048 <halt>

0000000050000024 <is_prime>:
    50000024:   4281                    li      t0,0
    50000026:   4305                    li      t1,1
    50000028:   00a35e63                bge     t1,a0,50000044 <.prime_loop_end>
    5000002c:   4285                    li      t0,1
    5000002e:   4309                    li      t1,2
    50000030:   83aa                    mv      t2,a0

0000000050000032 <.prime_loop_cond>:
    50000032:   00735963                bge     t1,t2,50000044 <.prime_loop_end>

0000000050000036 <.prime_loop_body>:
    50000036:   0263ee33                rem     t3,t2,t1
    5000003a:   000e0463                beqz    t3,50000042 <.prime_not_prime>
    5000003e:   0305                    addi    t1,t1,1
    50000040:   bfcd                    j       50000032 <.prime_loop_cond>

0000000050000042 <.prime_not_prime>:
    50000042:   4281                    li      t0,0

0000000050000044 <.prime_loop_end>:
    50000044:   8516                    mv      a0,t0
    50000046:   8082                    ret

0000000050000048 <halt>:
    50000048:   0001                    nop
    5000004a:   bffd                    j       50000048 <halt>


```

---

```
$ cat ycp.S
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

```

前20个数的结果

```
(gdb) print *(int(*)[20])0x5f000000
$2 = {0, 1, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1, 0}
```

可以看到，2, 3, 5, 7, 11, 13, 17, 19是质数，结果正确

---

```
$ ls
Makefile  README.md  build/  result.md  riscv.lds  ycp*  ycp.S
```

---

