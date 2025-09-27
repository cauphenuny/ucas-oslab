## Makefile 分析

```
# -----------------------------------------------------------------------
# Build and Debug Tools
# -----------------------------------------------------------------------

CROSS_PREFIX    = riscv64-unknown-linux-gnu-
CC                              = $(CROSS_PREFIX)gcc
GDB                             = $(CROSS_PREFIX)gdb
QEMU                    = $(DIR_QEMU)/riscv64-softmmu/qemu-system-riscv64

# -----------------------------------------------------------------------
# Build/Debug Flags and Variables
# -----------------------------------------------------------------------

CFLAGS                  = -O0 -fno-builtin -nostdlib -nostdinc -Wall -mcmodel=medany -ggdb3
USER_CFLAGS             = $(CFLAGS) -Wl,--defsym=TEXT_START=$(USER_ENTRYPOINT) -T riscv.lds

QEMU_OPTS               = -nographic -machine virt -m 256M -kernel $(ELF_USER) -bios none
QEMU_DEBUG_OPT  = -s -S

# -----------------------------------------------------------------------
# UCAS-OS Entrypoints and Variables
# -----------------------------------------------------------------------

USER_ENTRYPOINT                 = 0x50000000
```

- `CFLAGS`：编译器选项。  
  - `-O0`：无优化，便于调试。
  - `-fno-builtin`：禁用内建函数，适合裸机开发。
  - `-nostdlib -nostdinc`：不使用标准库和头文件，适合操作系统或裸机环境。
  - `-Wall`：开启所有警告。
  - `-mcmodel=medany`：RISC-V 地址模型，支持任意地址访问。
  - `-ggdb3`：生成详细的调试信息。

- `USER_CFLAGS`：用户程序编译选项。  
  - 包含 `CFLAGS`。
  - `-Wl,--defsym=TEXT_START=$(USER_ENTRYPOINT)`：链接器选项，定义程序入口地址为 `USER_ENTRYPOINT`。
  - `-T riscv.lds`：指定链接脚本。

- `QEMU_OPTS`：QEMU 启动参数。  
  - `-nographic`：无图形界面，使用终端。
  - `-machine virt -m 256M`：虚拟机类型和内存大小。
  - `-kernel $(ELF_USER)`：加载编译好的用户程序。
  - `-bios none`：不加载 BIOS。

- `QEMU_DEBUG_OPT`：QEMU 调试参数。  
  - `-s`：开启 GDB 远程调试端口（默认 1234）。
  - `-S`：启动后暂停 CPU，等待调试器连接。

- `USER_ENTRYPOINT`：用户程序入口地址，裸机程序从此地址开始执行（0x50000000）。

> [!info] RISC-V 地址模型
> - **medany**（medium any）：  
  支持代码和数据位于任意 2GB 地址空间，适合嵌入式或操作系统开发。代码通过 PC-relative addressing 访问数据和函数，允许程序运行在高地址（如 0x50000000），不受低地址限制。
> - **medlow**：  
  代码和数据都必须在低 2GB 地址空间（0x0 ~ 0x7FFFFFFF），适合普通应用程序，访问更高地址会出错。

---

## 小型测试

```c
.global main

msg: .string "Hello, World!\n"
len = . - msg

main:
    add x1, x0, 1
```

启动：
qemu virt机器启动地址：`0x1000`

```c
(lldb) dis -s 0x1000 -e 0x1018
    0x1000: auipc  t0, 0x0
    0x1004: addi   a2, t0, 0x28
    0x1008: csrr   a0, mhartid
    0x100c: ld     a1, 0x20(t0)
    0x1010: ld     t0, 0x18(t0)
    0x1014: jr     t0
```

将 PC (`0x1000`) 放到 t0，a2放 PC+0x28 (`0x1028`)，将当前hart id读到 a0，加载`0x1020` 处数据到 a1，加载 `0x1018` 处数据到 t0，跳转到 t0

`0x1018` 附近数据：

```c
(lldb) memory read -fx -s4 -c4 0x1018
0x00001018: 0x50000000 0x00000000 0x5fe00000 0x00000000
```

可以看到，`0x1018` 存的正是前面makefile中设置的 elf 入口点

此时寄存器状态：
```c
(lldb) reg read t0 a0 a1 a2
      t0 = 0x0000000050000000  main`_ftext
      a0 = 0x0000000000000000
      a1 = 0x000000005fe00000
      a2 = 0x0000000000001028
```

有个问题：为什么 `readelf` 显示的是 `0x5000000f`? 这地址甚至都没有 2 字节对齐

```
$ readelf -h main
ELF Header:
  Magic:   7f 45 4c 46 02 01 01 00 00 00 00 00 00 00 00 00 
  Class:                             ELF64
  Data:                              2's complement, little endian
  Version:                           1 (current)
  OS/ABI:                            UNIX - System V
  ABI Version:                       0
  Type:                              EXEC (Executable file)
  Machine:                           RISC-V
  Version:                           0x1
  Entry point address:               0x5000000f
  Start of program headers:          64 (bytes into file)
  Start of section headers:          5448 (bytes into file)
  Flags:                             0x5, RVC, double-float ABI
  Size of this header:               64 (bytes)
  Size of program headers:           56 (bytes)
  Number of program headers:         2
  Size of section headers:           64 (bytes)
  Number of section headers:         12
  Section header string table index: 11
```

`0x5000000f` 确实是main的位置，而 `0x50000000` 是 `_ftext`

```c
(lldb) dis -s 0x5000000f
main`main:
    0x5000000f <+0>: li     ra, 0x1

(lldb) image lookup -n _ftext
1 match found in os-lab/source/main:
        Address: main[0x0000000050000000] (main.PT_LOAD[0]..text + 0)
        Summary: main`_ftext
```

这一段 `_ftext` 只有 5 条指令

```c
(lldb) dis
main`_ftext:
->  0x50000000 <+0>:  ld     a0, 0x88(a0)
    0x50000002 <+2>:  ld     a1, 0xd8(s0)
    0x50000004 <+4>:  jal    s8, 0x50002576
    0x50000008 <+8>:  jal    tp, 0x500c764e
    0x5000000c <+12>: addi   s4, s4, 0x8
```

这些指令看起来非常奇怪

```
(lldb) mem read -fc -s1 -c32 0x50000000
0x50000000: Hello, World!\n\0\x93\0\x10\0\0\0\0\0\0\0\0\0\0\0\0\0\0
```

搞错了，msg应该放 `.data` section 的（难怪没对齐）（好蠢的错误

删除所有无关代码

```
.global main

main:
    add x1, x0, 1
```

entrance 正常了

```
 readelf -h main
ELF Header:
  Magic:   7f 45 4c 46 02 01 01 00 00 00 00 00 00 00 00 00 
  Class:                             ELF64
  Data:                              2's complement, little endian
  Version:                           1 (current)
  OS/ABI:                            UNIX - System V
  ABI Version:                       0
  Type:                              EXEC (Executable file)
  Machine:                           RISC-V
  Version:                           0x1
  Entry point address:               0x50000000
  Start of program headers:          64 (bytes into file)
  Start of section headers:          5328 (bytes into file)
  Flags:                             0x5, RVC, double-float ABI
  Size of this header:               64 (bytes)
  Size of program headers:           56 (bytes)
  Number of program headers:         2
  Size of section headers:           64 (bytes)
  Number of section headers:         12
  Section header string table index: 11
```

---

## Task1:

```
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
.equ retval_addr, 0x50001000
```

累加 0-50。

似乎 `0x60000000` 区域地址不可写，为常量 `0xffffffff`，`0x50001000` 区域是正常的

超过 RAM 大小了？256M=`0x10000000`，测试一下 `0x5ffffffc`

`0x5ffffffc` 可写，所以RAM加载的地址范围是 `[0x50000000, 0x60000000)`

 