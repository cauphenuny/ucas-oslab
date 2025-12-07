# Project 4 设计文档

## 1. 基础数据结构与管理策略

### 1.1 PTE、地址类型与工具函数

- **PTE 结构**：`arch/riscv/include/pgtable.h` 以 `typedef uint64_t PTE` 表示 SV39 条目，宏 `_PAGE_PRESENT/_PAGE_READ/.../_PAGE_SOFT` 描述 10 个低位标志，高位 `_PAGE_PFN_SHIFT` 之后保存物理页号。
- **辅助 API**：`get_pa()`/`get_pfn()`/`set_pfn()`/`get_attribute()`/`set_attribute()`/`clear_attribute()`/`clear_pgdir()` 提供无锁位操作，所有页表修改路径（例如 `bind_page()`、`swapin()`）都通过这些函数保持一致。
- **地址类型**：
  - `kva_t`（kernel virtual address）和 `pa_t` 之间由 `kva2pa()`、`pa2kva()` 做遮罩/拼接，确保仅处理 `0xffffffc0_xxxx` 范围。
  - `uva_t`（user virtual address）必须通过 `uva2kva()` 转换来访问；该函数内部会调用 `alloc_page()` 保证页已分配或换入。
- **`uva_object_t`**：定义在 `include/os/mm.hpp`，是对 `uva_t` 的面向对象封装。模板化的 `get<T>()` / `set<T>()` / 隐式 `operator T&()` 在访问前确保一页内完成、触发 `alloc_page()`、并返回映射到的 KVA。Mailbox、Pipe 等跨页 IPC 全量使用该封装来规避用户地址换出后的悬挂指针。

```c
typedef uint64_t PTE;

#define _PAGE_PRESENT (1 << 0)
#define _PAGE_READ    (1 << 1)
#define _PAGE_WRITE   (1 << 2)
#define _PAGE_EXEC    (1 << 3)
#define _PAGE_USER    (1 << 4)
#define _PAGE_SOFT    (1 << 8)

static inline pa_t get_pa(PTE entry) {
  return (entry >> _PAGE_PFN_SHIFT) << NORMAL_PAGE_SHIFT;
}

static inline long get_pfn(PTE entry) { return entry >> _PAGE_PFN_SHIFT; }
static inline void set_pfn(PTE* entry, uint64_t pfn) {
  *entry = (*entry & ((1lu << _PAGE_PFN_SHIFT) - 1)) | (pfn << _PAGE_PFN_SHIFT);
}
```

### 1.2 `pageframe_t`

- 位于 `include/os/mm.h`，由两个字段组成：
  - `list_node_t group_node`：挂接到所属 pageframe_group 的链表，用于遍历、置换。
  - `PTE* pte`：记录当前绑定的叶子 PTE（缺页或解绑时置空）。
- 工具函数：`pageframe_kva2id()`/`pageframe_attr2kva()`/`pageframe_kva2attr()` 提供 KVA 与 `pageframe_t` 之间的映射，`pageframe_destruct()` 在换出或释放时负责清除 `pte` 并调用 `free_pageframe()`。

```c
typedef struct pageframe {
  list_node_t group_node;
  PTE* pte;
} pageframe_t;

extern pageframe_t pages[MAX_PAGE_NUM];
static inline int pageframe_kva2id(ptr_t addr) { return (addr - FREEMEM_KERNEL) / PAGE_SIZE; }
static inline ptr_t pageframe_attr2kva(pageframe_t* pf) {
  return FREEMEM_KERNEL + (pf - pages) * PAGE_SIZE;
}
```

### 1.3 `pageframe_group_t` 与 `pagegroup_vtable`

- `pageframe_group_t` 字段：
  - `list_t pages`：维护所有页框（含页表页与叶子页）的顺序；`list.name` 作为 `info page` 输出的组名。
  - `size_t capacity/used`：以页为单位的配额与当前使用量，`shrink_pagegroup()` 根据二者决定是否触发 `swapout()`。
  - `int refcount`：允许同一个组被多个 PCB 共享，典型场景是 `fork_pagegroup()` 后旧组/新组的引用调整。
  - `pagegroup_vtable_t* vtable`：指向 FIFO/SC/LRU 任一算法的虚表。

```c
typedef struct pageframe_group {
  list_t pages;
  size_t capacity;
  size_t used;
  int refcount;
  pagegroup_vtable_t* vtable;
} pageframe_group_t;

typedef const struct pagegroup_vtable {
  void (*init)(pageframe_group_t*);
  void (*cleanup)(pageframe_group_t*);
  void (*attach)(pageframe_group_t*, int pageframe_id);
  void (*detach)(pageframe_group_t*, int pageframe_id);
  list_node_t* (*evict)(pageframe_group_t*);
  void (*show)(pageframe_group_t*, struct pageframe*);
  const char* name;
} pagegroup_vtable_t;
```
- **回调触发点**：
  - `init/cleanup`：当 `sys_set_page_repl_algo()` 切换算法时调用，允许算法初始化私有状态（例如 LRU 的 `last_accessed[]`）。
  - `attach/detach`：`alloc_pageframe()` 成功后调用 `group->vtable->attach()` 将新页插入链表并递增 `used`；`free_pageframe()` 则调用 `detach()` 将其移除。
  - `on_timer`：`handle_irq_timer()` 在每次时钟中断时触发。
  - `on_access/on_write`：`handle_page_fault()` 捕获已经存在的 PTE 但因权限/软标志导致的异常，对 LOAD/INST fault 调用 `on_access()`，对 STORE fault 调用 `on_write()`，以更新 `_PAGE_ACCESSED/_PAGE_DIRTY` 或自定义状态。
  - `evict`：`swapout()` 在无法直接满足 `shrink_pagegroup()` 要求时调用该回调选出 victim（FIFO/SC/LRU 在 `kernel/mm/pf_evict.cpp` 中具体实现）。
  - `show`：`show_pagegroups()` 遍历链表时调用，用于根据叶子/页表页输出不同的信息（例如 LRU 会打印 `last_accessed`）。

### 1.4 组与页的整体生命周期

1. **创建**：`init_pagegroup()` 在启动阶段初始化内核保留组 `PAGE_GROUP_KERNEL`，容量覆盖 `MAX_PAGE_NUM`。用户进程第一次 `exec` 时，`new_top_pgdir()` 会 `alloc_pageframe()` 并把 pgdir 自身挂到该组。
2. **分配**：`alloc_page()` → `find_pagegroup(pgdir)` → `alloc_pageframe(group, 1)` → `bind_page()` 保证 PTE 与页框关联；`bind_page()` 会刷新 TLB 并在 `pageframe_t` 中记录 PTE。
3. **收缩**：`shrink_pagegroup(group, needed)` 在任何新页申请前调用，若 `used + needed > capacity` 则循环执行 `swapout()`，直至释放足够名额。
4. **fork / resize**：
   - `fork_pagegroup(pgdir, capacity, name)`：分裂原组容量、引用计数并迁移整棵页表中的页框（`migrate()` 递归复制页表页并重新挂接叶子页）。
   - `resize_pagegroup(group, new_capacity)`：通过向内核组借页或归还页实现动态调整，并在缩小时使用 `shrink_pagegroup()` 立即触发换出。
5. **回收**：进程退出时 `cleanup_vm()` 会 `free_top_pgdir()`，递归释放所有页表页/叶子页、清空 `_PAGE_SOFT` 的 swap 块，并在 `refcount` 归零时 `free_pagegroup()`。

## 2. Swap 与缺页路径

### 2.1 元数据与容量

- `swap_base_location`：由 `createimage` 写入镜像参数并在 `init_task_info()` 中读取，告诉内核 SD 上的起始扇区。
- 常量：`SWAP_SIZE = 64 MiB`、`NUM_MAX_SWAP = SWAP_SIZE / PAGE_SIZE`、`SWAP_LEN = PAGE_SIZE / SECTOR_SIZE`。
- 状态：
  - `swap_using[]`（位映射）与 `swap_used/swap_next_idx` 实现近似 round-robin 的空槽搜索。
  - `swap_counter_in/out` 记录 swap 统计，供 `show_swap()` 输出。

### 2.2 `swapout()` 流程（`kernel/mm/swap.c`）

1. `pageframe_group_t` 调用 `vtable->evict()` 找到可换出的 `list_node_t`（只考虑叶子页）。
2. 取出对应 `pageframe_t`：
   - 保存当前 `PTE*`，使用 `alloc_swap()` 分配一个 `swap_id`。
   - `bios_sd_write()` 将整页写入 `swap_base_location + swap_id * SWAP_LEN`。
3. 更新 PTE：清掉 `_PAGE_PRESENT`，设置 `_PAGE_SOFT` 并写入 `swap_id`；随后 `pageframe_destruct()` 解绑页框并回收物理页。
4. `local_flush_tlb_all()` 确保所有 hart 不再缓存旧映射，`swap_counter_out++`。
5. 若找不到 victim（例如全是页表页），会遍历 PCB 找出使用该组的进程并触发 `do_kill()`/`do_exit()`，防止死锁。

### 2.3 `swapin()` 流程

1. 根据 `uva` 逐级定位三级页表（`get_vpn()` + `find_pte()`），确保 L2/L1 皆为 Present。
2. 读取 level-0 PTE，断言 `_PAGE_SOFT` 置位后取出 `swap_id`。
3. 调用 `bios_sd_read()` 将磁盘数据读入 `alloc_pageframe()` 预先准备的物理页。
4. 通过 `bind_page()` 重新赋予 `_PAGE_USER|_PAGE_READ|_PAGE_WRITE|_PAGE_EXEC` 权限，释放 `swap_id`，`swap_counter_in++`。

### 2.4 触发点与清理

- **触发点**：
  - `alloc_pageframe()` 可能因为容量不足而调用 `swapout()`。
  - `handle_page_fault()` 在遇到 `_PAGE_SOFT` PTE 时调用 `swapin()`，在遇到未分配 PTE 时直接 `alloc_page()`。
- **清理**：`free_pgdir()` 在扫描页表时若命中 `_PAGE_SOFT` 条目会 `free_swap()`；`cleanup_vm()` 保证进程退出后不会遗留磁盘块。
- **观测**：`show_swap()` 接入 `info swap` 子命令，输出当前容量/占用/计数。

## 3. Pipe 设计

### 3.1 数据结构

- `pipe_entry_t`（`kernel/mm/pipe.c`）
  - `char name[32]`、`bool used/closing`。
  - `spin_lock_t lock`：保护段链表与引用计数。
  - `list_t segments`：按 FIFO 顺序存储 `pipe_segment_t`。
  - `list_t reader_wait_list`：读方阻塞队列，配合 `do_block()`/`unblock_list()`。
  - `size_t buffered_bytes`、`int refcnt`、`pid_bitmap_t ref_bitmap`：方便统计及 `cleanup_pipe()` 定位需要释放的进程。
- `pipe_segment_t`
  - `pipe_segment_type_t type`：`PIPE_SEG_PHYS` 或 `PIPE_SEG_SWAP`。
  - `kva_t page` / `uint64_t swap_id`：根据类型记录数据来源。
  - `size_t length`（固定为 `PIPE_PAGE_SIZE`）。
- 全局：`pipe_table[32]` + `pipe_table_lock` 管理目录；`pipe_system_init()` 负责一次性初始化。

```c
typedef enum {
  PIPE_SEG_PHYS,
  PIPE_SEG_SWAP,
} pipe_segment_type_t;

typedef struct pipe_segment {
  list_node_t node;
  pipe_segment_type_t type;
  kva_t page;
  uint64_t swap_id;
  size_t length;
} pipe_segment_t;

typedef struct pipe_entry {
  bool used;
  char name[PIPE_NAME_MAX_LEN];
  spin_lock_t lock;
  list_t segments;
  list_t reader_wait_list;
  size_t buffered_bytes;
  int refcnt;
  pid_bitmap_t ref_bitmap;
  bool closing;
} pipe_entry_t;
```

### 3.2 写路径（`pipe_give_pages()`）

1. 校验输入：必须按页对齐、`length` 是 `PIPE_PAGE_SIZE` 的整数倍。
2. 逐页调用 `pipe_detach_page_from_sender()`：
   - `find_pte()` 找到写进程的 PTE。
   - 若 `_PAGE_PRESENT`：取出物理页，`attr->pte = NULL`，`detach_pageframe()`（保持发送方 pagegroup 的配额准确），将 PTE 置零。
   - 若 `_PAGE_SOFT`：只需要记录 `swap_id` 并清空 PTE。
3. 为每个页面构造 `pipe_segment_t`（使用 `kmalloc()`），插入目标 pipe 的 `segments` 链表；失败时释放 segment 并中断。
4. `pipe_push_segment()` 成功后唤醒 `reader_wait_list`，返回传输成功的字节总数。

### 3.3 读路径（`pipe_take_pages()`）

1. 仍需按页对齐请求。
2. 对每一页：
   - `pipe_pop_segment()` 从 `segments` 链表头取出一个段，若为空则阻塞到 `reader_wait_list`（除非 pipe 已关闭）。
   - `find_pte()` 创建/定位目标地址的 PTE，调用 `pipe_clear_dest_mapping()` 清理旧映射（含 `_PAGE_SOFT` 的 swap 块）。
   - `pipe_install_segment_to_dest()` 根据段类型：
     - `PIPE_SEG_PHYS`：`attach_pageframe()` 到读者的 pagegroup，并 `bind_page()`。
     - `PIPE_SEG_SWAP`：只设置 `_PAGE_SOFT` 和 `swap_id`，保持延迟换入的语义。
   - `local_flush_tlb_page(dest_va)` 保证立刻可见，最后 `kfree(seg)`。

### 3.4 生命周期与清理

- `pipe_open()`：全局查找名字，若存在则增加 `refcnt` 和 `ref_bitmap`；否则在首个空槽创建并初始化。
- `cleanup_pipe(pid)`：由 `kernel/sched/sched.cpp` 在 PCB 退出时调用。它遍历所有 pipe，清除 `ref_bitmap` 中对应 bit，`refcnt` 归零则 `pipe_destroy_locked()` 释放所有 segment 并唤醒在 `reader_wait_list` 上的阻塞进程。
- `pipe_entry_clear()` 重置结构体，把 `used` 置零便于下次复用。

### 3.5 与 swap/测试的联动

- 因为段可以保存 `_PAGE_SOFT` 状态，pipe 支持在发送端页已换出的情况下继续传输，并在接收端延后换入，保证与总体内存策略一致。
- `test/test_project4/pipe.c` 拆出自测（同进程 give/take）与多进程 demo，确保 `sys_exec` + pipe 协同工作；`ipc.c` 则比较 mailbox 与 pipe 的吞吐差异，强调零拷贝的优势。

## 4. 其他修复与增强

### 4.1 内核内部

- **Buddy `kmalloc/kfree`**（`kernel/mm/kmalloc.c`）：预留 4 MiB 内核堆，最小块 16 B，解决 Pipe 段等短生命周期对象的申请问题，避免静态数组撑爆。
- **日志串行化**：`kernel/utils/logger.c` 新增 `spin_lock_t logger_lock`，所有 `pretty_log*` 调用会串行输出，避免多核/多线程日志交错。
- **Mailbox 安全性**：在 `kernel/locking/mailbox.cpp` 中将 `do_mbox_send/recv` 的用户指针改为 `uva_object_t`，结合 P4 的按需换页能力保证大消息可在换出后仍安全传输。
- **Pagegroup 工具链**：`sys_set_max_memory()` 利用 `fork_pagegroup()`/`resize_pagegroup()` 为每个进程设置独立配额；`sys_set_page_repl_algo()` 允许运行期切换置换策略。
- **修复调度问题**：在 `sched.cpp` 中的switch_to前后释放并从新获取内核锁，从而使得操作系统能够在尚未处理完已进入内核的进程时允许新进程进入内核

### 4.2 用户态与观测

- **Shell 指令**：`free`（基于 `sys_get_free_memory()`）、`time`（对任意命令计时）、`watch`（周期刷新命令输出）。
- **Info 面板**：`info page` 和 `info swap` 直接调用 `show_pagegroups()`、`show_swap()`，让换页策略、配额、命中数据可视化。
- **计时/测试新增 syscall**：`sys_get_proc_tick()` 返回当前进程累计 CPU tick，`test/test_project4/swap.c` 用来统计缺页延迟；`sys_pipe_*`/`sys_set_max_memory` 等接口已同步到 `tiny_libc/include/unistd.h`，保证用户态调用一致。
- **测试基准**：`test/test_project4/` 目录新增 `swap.c`、`pipe.c`、`ipc.c`、`oom.c` 等针对性样例，覆盖置换策略、零拷贝通道以及配额调整等关键路径。
