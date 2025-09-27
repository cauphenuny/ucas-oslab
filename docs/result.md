```
$ riscv64-unknown-linux-gnu-objdump -d ycp

ycp:     file format elf64-littleriscv


Disassembly of section .text:

0000000050000000 <_ftext>:
    50000000:   4305                    li      t1,1
    50000002:   4381                    li      t2,0
    50000004:   03200e13                li      t3,50
    50000008:   a009                    j       5000000a <start>

000000005000000a <start>:
    5000000a:   006e4563                blt     t3,t1,50000014 <end>
    5000000e:   939a                    add     t2,t2,t1
    50000010:   0305                    addi    t1,t1,1
    50000012:   bfe5                    j       5000000a <start>

0000000050000014 <end>:
    50000014:   10000e17                auipc   t3,0x10000
    50000018:   fe8e0e13                addi    t3,t3,-24 # 5ffffffc <retval_addr>
    5000001c:   007e2023                sw      t2,0(t3)
    50000020:   a009                    j       50000022 <halt>

0000000050000022 <halt>:
    50000022:   0001                    nop
    50000024:   bffd                    j       50000022 <halt>
```

```
$ cat ycp.S
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
```

```
$ ls
Makefile  README.md  build/  result.md  riscv.lds  ycp*  ycp.S
```

```
(lldb) process connect connect://ubuntu20.orb.local:1234
Process 1 stopped
* thread #1, stop reason = signal SIGTRAP
    frame #0: 0x0000000000001000
->  0x1000: auipc  t0, 0x0
    0x1004: addi   a2, t0, 0x28
    0x1008: csrr   a0, mhartid
    0x100c: ld     a1, 0x20(t0)
(lldb) b *0x50000000
Breakpoint 1: where = ycp`_ftext, address = 0x0000000050000000
(lldb) b halt
Breakpoint 2: where = ycp`halt + 2, address = 0x0000000050000024
(lldb) c
Process 1 resuming
Process 1 stopped
* thread #1, stop reason = breakpoint 1.1
    frame #0: 0x0000000050000000 ycp`_ftext at ycp.S:4
   1    .global main
   2   
   3    main:
-> 4        li t1, 1      # i = 1
   5        li t2, 0      # sum = 0
   6        li t3, 50
   7        j start
(lldb) c
Process 1 resuming
Process 1 stopped
* thread #1, stop reason = breakpoint 2.1
    frame #0: 0x0000000050000024 ycp`halt at ycp.S:22
   19  
   20   halt:
   21       nop
-> 22       j halt
   23  
   24   .section data
   25   .equ retval_addr, 0x5ffffffc
(lldb) print *(int*)0x5ffffffc
(int) 1275
```