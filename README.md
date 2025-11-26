# UCAS-OS-Lab

## P1

C-Core

---

## P2

C-Core

### Launch:

```
./configure
make
make run
# or make debug
```

---

## P3

C-Core

### Launch:

```
./configure
make

# single core
make run # or debug

# multi core
make run-smp # or debug-smp
```

### Features:

- 使用大内核锁

- 调度时将 `current_running(tp)` 存在 `sscratch` 中。

- 将 `lock.c` 迁移到 `lock.cpp`，从而可以使用 C++ 的 RAII 机制，自动管理锁的获取与释放。

   ```
   {
      with_spin guard(&some_spinlock);
      ...
   }
   ```

   这样自动管理的好处是控制流比较复杂时（例如中途有 return, break 等），也能保证锁的正确释放，避免死锁。

- shell 功能：

  - 支持含有语义的高亮，正确/错误命令分别以不同颜色显示

  - 支持 tab 自动补全

  - 支持 Ctrl-W, Ctrl-U 删除

  - 支持 Ctrl-N, Ctrl-P 切换保存的上一条命令

  - 自动滚屏，高度可以运行时设置

  - 所有命令：

   ```
   > root@UCAS_OS: help
   usage: command [subcmd ...]
   echo:       echo
   ts:         show task
   ps:         show process
   exec:       execute program
   kill:       kill process
   clear:      clear screen
   taskset:    set task affinity
   top:        show top processes
   info:       show system info
   set_height: set shell height
   nice:       set scheduling nice value
   help:       show help information
   exit:       exit shell
   ```

  - `ps` 显示 process id, parent process id, name, status, channel, CPU 占用百分比，CPU 亲和性，内存占用量，nice 值等信息

   ```
   > root@UCAS_OS: top
   PID  PPID  NAME            STATUS    CHANNEL  CPU   AFF  MEM/K  MEM/U  NI
   0    N/A   init            READY     ready    0%    10   624    N/A    0
   1    N/A   init            READY     ready    0%    01   624    N/A    0
   2    0     shell           RUNNING   cpu0     16%   11   0      224    20
   3    2     mbox_exchange   BLOCKED   proc     0%    11   560    96     0
   4    3     proc0           BLOCKED   proc     0%    11   560    80     0
   5    3     proc1           BLOCKED   proc     0%    11   560    80     0
   6    4     p0-recv         READY     ready    47%   11   608    64     0
   7    4     p0-send         READY     ready    47%   11   512    64     0
   8    5     p1-recv         RUNNING   cpu1     44%   11   0      64     0
   9    5     p1-send         READY     ready    46%   11   704    64     0
   ```

     channel 分为 ready, cpuX, proc, mutex, cond, sema, bar，分别表示就绪队列、在 cpuX 上运行、等待其他进程、等待互斥锁、等待条件变量、等待信号量、等待屏障。

  - `top` 命令会自动循环执行 `ps` 直到按下任意键

  - `info` 命令支持查看多种系统信息:

   ```
   > root@UCAS_OS: info help
   usage: info [subcmd ...]
   task:      display runnable tasks
   proc:      display current processes
   ptree:     display process tree
   pcb:       display pcb array
   time:      display timer and cputime
   cond:      display condition status
   mutex:     display mutex status
   bar:       display barrier status
   sema:      display semaphore status
   sync:      display all synchronization machanics
   mbox:      display mailbox status
   help:      display this help message
   ```

   例如：
   ```
   > root@UCAS_OS: info sync
   mutex 0: key=42, ref=0x18, acquired=0, pid=11
   condition 0: key=58, ref=0x18, waiting_count=3
   ```

   ```
   > root@UCAS_OS: info ptree
   init (pid=0)
   |-> shell (pid=2)
      |-> condition (pid=13)
      |   |-> producer (pid=14)
      |   |-> consumer (pid=15)
      |   |-> consumer (pid=16)
      |   |-> consumer (pid=17)
      |-> barrier (pid=18)
         |-> test_barrier (pid=19)
         |-> test_barrier (pid=20)
         |-> test_barrier (pid=21)
   init (pid=1)
   ```

   ```
   > root@UCAS_OS: exec mbox_exchange &
   > root@UCAS_OS: info time
   timer: ticks=69005630, time_base=10000000, timer_interval=25000
   cpu0: sys=19731346(37%), user=32627072(62%), idle=0(0%)
   cpu1: sys=7477614(14%), user=6050073(11%), idle=38825224(74%)
   > root@UCAS_OS: info time
   timer: ticks=115351076, time_base=10000000, timer_interval=25000
   cpu0: sys=89264343(77%), user=26086699(22%), idle=0(0%)
   cpu1: sys=21193853(45%), user=25119890(54%), idle=0(0%)
   ```

  - `kill` 命令会结束进程树，并回收所有资源（包括 mutex, cond, barrier, semaphore, mailbox 等）

  - `nice` 命令可以调整当前进程优先级 (非负整数)，值越高优先级越低

![shell](docs/p3/shell.png)
