## 🛠️ Part 6: 页面置换算法 (Page Replacement) 核心设计

本阶段在 Part 3 内存懒分配与 VMA 的基础上，实现了**内存模拟 Swap 分区**以及 **FIFO（先进先出）**与 **LRU（最近最少使用）**两种页面置换算法。当进程 mmap 区域驻留的物理页数量超过设定上限时，内核将自动触发页面换出与换入流程。

### 1. 核心数据结构
* [cite_start]**`struct VMA_page`**：追踪 mmap 页面的状态（`UNUSED` / `INMEM` / `SWAPPED`），并记录进入内存时间戳 `last_in_mem_time`（FIFO 用）与最后访问时间戳 `last_access_time`（LRU 用） [cite: 545, 546, 550, 551, 552, 555, 556]。
* [cite_start]**`mock_swap` 全局交换区**：在内存中开辟静态数组，每个 Slot 存储一个完整物理页（4KB），模拟磁盘 Swap 分区 [cite: 566, 567, 568]。
* [cite_start]**PCB 扩展**：在 `struct proc` 中新增 `max_page_in_mem`（允许驻留的最大页数）、`cur_page_in_mem`（当前驻留页数）和 `page_swap_count`（累计换出次数） [cite: 557, 563, 564, 565]。

### 2. 新增系统调用
* [cite_start]`set_max_page_in_mem(int max)` (SYS_54332)：动态设置最大驻留物理页上限 [cite: 570]。
* [cite_start]`get_swap_count()` (SYS_54333)：获取当前进程因超限引发的累计页面换出次数 [cite: 570]。
* [cite_start]`lru_access_notify(uint64 va)` (SYS_54334)：**【LRU 关键】** 由于硬件未提供访问位，由用户态程序在访问页面后主动通知内核，刷新该页的时间戳 [cite: 573, 574]。

### 3. 置换算法对比与测试结果
[cite_start]对于测试序列 `0,1,2,3,0,1,4,5,0,1,6,7,0,1,2,3`（容量上限为 4） [cite: 596]：
* [cite_start]**FIFO 算法**：踢出依据为最早进入内存的页 [cite: 588][cite_start]。在 `swap_in` 换入页面后精确刷新了 `last_in_mem_time`，成功规避了重复换出循环的 Bug，最终**实际换出 8 次**，完美通过 `test_vm_fifo` [cite: 576, 584, 590, 595]。
* [cite_start]**LRU 算法**：结合用户态通知，精准剔除最久未访问的页面 [cite: 588][cite_start]。由于局部性原理，**实际换出仅 6 次**，性能明显优于 FIFO，完美通过 `test_vm_lru` [cite: 588, 595]。

### 4. 双页表解映射安全顺序
在 `swap_out` 执行物理页写回并释放时，严格遵循以下顺序以规避内核 panic：
1. [cite_start]先解除内核页表映射（`kpagetable`），且设置 `do_free=0` [cite: 576, 578]。
2. [cite_start]再解除用户页表映射（`pagetable`），且设置 `do_free=1` 释放物理页 [cite: 576, 579]。