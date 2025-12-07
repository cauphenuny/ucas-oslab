# Project 4 概述

## 虚拟内存与页表体系

- **SV39 启动与多核复位**：两阶段启动：
  1. 主核 boot，进入 `init/main.c:main()` 完成 `init_jmptab()` 后通知其他核 boot，等待其他所有核启动到进入 `init/main.c:main()` 后清理 boot 页表 `reset_boot_vm()`
  2. 主核继续进行其他的初始化，从核等待初始化完成。
- **页表管理 API**：`kernel/mm/mm.c` 提供 `alloc_page()`、`bind_page()`、`uva2kva()`、`cleanup_vm()` 等函数，配合 `share_pgtable()` 实现「用户态私有 + 内核共享」的页表层级管理。
- **页框分配与追踪**：每个物理页在 `pageframe_t` 中记录其挂接的 `PTE*`，通过 `pageframe_kva2attr()`、`pageframe_destruct()` 在释放时回收；`alloc_pageframe()` 会在分配前调用 `shrink_pagegroup()` 触发必要的换页操作。

## 页帧组与置换算法

- **配额化的 pageframe_group**：`include/os/mm.h` 定义的 `pageframe_group_t` 让每个地址空间拥有独立容量、引用计数与页链表，支持 `fork_pagegroup()`、`resize_pagegroup()` 与 `free_pagegroup()` 以运行时调整额度。
- **可插拔置换策略**：`kernel/mm/pf_evict.cpp` 实现 FIFO、Second-Chance、LRU 三种 `pagegroup_vtable`。通过 `sys_set_page_repl_algo()`（见 `kernel/syscall/syscall.c`）可在运行中切换算法，`handle_irq_timer()`/`handle_page_fault()` 负责调用 `on_timer`、`on_access`、`on_write` 钩子维护状态。
- **运维指令**：`show_pagegroups()`（`kernel/mm/pagegroup.cpp`）接入 shell 的 `info page` 子命令，配合 `tui::display_table` 输出每个组的容量、占用、算法等细节，便于调优。

## 交换区与缺页处理

- **64 MiB 交换空间**：`kernel/mm/swap.c` 将镜像中的 `swap_base_location` 暴露给内核，最多管理 `NUM_MAX_SWAP = 64MiB / 4KiB` 个槽位，`swap_using[]` + `swap_used`/`swap_next_idx` 负责分配策略。
- **换入换出流程**：`swapout()` 通过组的 `evict()` 选出叶子页，使用 `bios_sd_write()` 落盘并把 PTE 转成 `_PAGE_SOFT` 状态；`swapin()` 则在缺页时 `bios_sd_read()` 回内存并重新 `bind_page()`。
- **缺页处理路径**：`handle_page_fault()`（`kernel/irq/irq.c`）先尝试懒分配，再判断 `_PAGE_SOFT` 触发 `swapin()`，同时对命中页调用 `on_access/on_write` 收集热度信息。
- **可观察性**：`show_swap()` 集成到 `info swap`，实时展示容量、已用交换槽以及换入/换出次数，方便实验分析。

## 内核动态内存与 UVA 工具

- **Buddy `kmalloc`**：`kernel/mm/kmalloc.c` 预留 4 MiB，提供 16 B 起步、最大 4 MiB 的伙伴分配器，解决 Pipe、IPC 等场景的临时对象申请问题。
- **`uva_object_t` 自动触页**：`include/os/mm.hpp` 提供模板化封装，发送/接收（例如 `kernel/locking/mailbox.cpp`）时统一通过 `alloc_page()` + `uva2kva()` 保障跨缺页/换页访问安全。

## Zero-copy Pipe

- **按页转移的数据通道**：`kernel/mm/pipe.c` 维护 `PIPE_MAX_COUNT=32` 个 `pipe_entry_t`。写端通过 `pipe_give_pages()` 把自身页或 swap 记录拆成 `pipe_segment_t` 放入段链表；读端调用 `pipe_take_pages()` 直接把段挂到目标页表，实现零拷贝传输信息。
- **与虚拟内存深度融合**：每个段记录 `pipe_segment_type_t`（物理/交换），在消费者侧若需要会重新 `attach_pageframe()` 或仅搬运 `_PAGE_SOFT` 元数据；`cleanup_pipe()` 由调度器在进程退出时回收引用，避免句柄泄露。
- **Syscall & 用户 API**：`sys_pipe_open/give_pages/take_pages` 同步暴露在 `tiny_libc/include/unistd.h`，`test/test_project4/pipe.c`、`ipc.c` 提供单进程与跨进程的验证样例。

## Shell 与观测工具

- **新命令**：`test/shell.c` 新增 `free`（`sys_get_free_memory()`）、`time`（基于 `sys_get_tick()`）、`watch`（周期刷新命令输出）等工具，支持运行期观察内存水位和命令耗时。
- **信息面板扩展**：`info page`、`info swap` 等子命令覆盖新模块；`ps`/`top` 显示每个进程的栈内存占用与 NICE 值。
- **计时原语**：`sys_get_proc_tick()`（`kernel/syscall/syscall.c`）暴露 per-process tick 计数，`test/test_project4/swap.c` 用它来衡量不同置换策略的缺页代价。

## 测试覆盖

`test/test_project4/` 下提供针对性 workload：

- `swap.c`：可切换 `lru/fifo/sc` 算法，统计缺页次数与用时。
- `pipe.c`、`ipc.c`：验证零拷贝 pipe 的自测与性能差异，并与 mailbox 进行吞吐对比。
- `oom.c`：通过 `sys_set_max_memory()` 人为收紧配额，测试页框组不足以放下页目录时操作系统是否能杀死进程。