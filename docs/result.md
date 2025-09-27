```
$ riscv64-unknown-linux-gnu-objdump -d ycp

ycp:     file format elf64-littleriscv


Disassembly of section .text:

0000000050000000 <_ftext>:
    50000000:   4561                    li      a0,24
    50000002:   006000ef                jal     50000008 <is_prime>
    50000006:   a839                    j       50000024 <halt>

0000000050000008 <is_prime>:
    50000008:   4285                    li      t0,1
    5000000a:   4309                    li      t1,2
    5000000c:   83aa                    mv      t2,a0

000000005000000e <loop_cond>:
    5000000e:   00735963                bge     t1,t2,50000020 <loop_end>

0000000050000012 <loop_body>:
    50000012:   0263ee33                rem     t3,t2,t1
    50000016:   000e0463                beqz    t3,5000001e <not_prime>
    5000001a:   0305                    addi    t1,t1,1
    5000001c:   bfcd                    j       5000000e <loop_cond>

000000005000001e <not_prime>:
    5000001e:   4281                    li      t0,0

0000000050000020 <loop_end>:
    50000020:   8516                    mv      a0,t0
    50000022:   8082                    ret

0000000050000024 <halt>:
    50000024:   0001                    nop
    50000026:   bffd                    j       50000024 <halt>
```

---

```
$ cat ycp.S
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

```

---

```
$ ls
Makefile  README.md  build/  result.md  riscv.lds  ycp*  ycp.S
```

---

传入质数 29：
```
(lldb) c
      t0 = 1342177280  ycp`_ftext
      t1 = 0
      t2 = 0
      a0 = 0
Process 1 resuming
Process 1 stopped
* thread #1, stop reason = breakpoint 1.1
    frame #0: 0x0000000050000000 ycp`_ftext at ycp.S:4
   1    .global main
   2   
   3    main:
-> 4        li a0, 29
   5        jal is_prime
   6        j halt
   7
(lldb) c
Process 1 resuming
      t0 = 1
      t1 = 29
      t2 = 29
      a0 = 1
Process 1 stopped
* thread #1, stop reason = breakpoint 2.1
    frame #0: 0x0000000050000026 ycp`halt at ycp.S:32
   29  
   30   halt:
   31       nop
-> 32       j halt
(lldb)  
```

传入合数 24:
```
* thread #1, stop reason = breakpoint 1.1
    frame #0: 0x0000000050000000 ycp`_ftext at ycp.S:4
   1    .global main
   2   
   3    main:
-> 4        li a0, 24
   5        jal is_prime
   6        j halt
   7
(lldb) c
Process 1 resuming
      t0 = 0
      t1 = 2
      t2 = 24
      a0 = 0
Process 1 stopped
* thread #1, stop reason = breakpoint 2.1
    frame #0: 0x0000000050000026 ycp`halt at ycp.S:32
   29  
   30   halt:
   31       nop
-> 32       j halt
(lldb)  
```

a0结果正确