# 💻 Peking University OS Labs - Part 2: Process Management

当前分支 (`process`) 包含了本人在北京大学操作系统课程实验 **Part 2（进程管理系统调用实现）** 的最终代码。

本阶段的核心目标是扩展 xv6 的进程控制能力（引入轻量级线程创建、精细化子进程等待），并深度重构内核的时钟计时体系以适配 QEMU 硬件环境。

## 🚀 核心实现与特性

本分支主要新增或重写了以下系统调用与底层机制：

* **线程机制 (`SYS_clone` - 220)**：实现了类似 Linux 的 `clone` 系统调用。支持为子线程指定独立的用户栈，并正确继承父进程上下文。
* **进程等待 (`SYS_wait4` - 260)**：在原生 `wait` 基础上，支持等待指定的 `target_pid`，并兼容 POSIX 规范的退出状态码格式。
* **高精度时间 (`SYS_gettimeofday` - 169 & `SYS_nanosleep` - 101)**：分别实现了基于硬件时钟微秒级查询，以及基于操作系统时钟中断的纳秒级睡眠。
* **进程基础控制**：补齐了 `getppid` (获取父进程) 和 `sched_yield` (主动让出 CPU) 等基础接口。

## 💡 避坑指南与底层细节 (Gotchas)

在实现过程中，有几个极易踩坑的底层细节，供后续参考：

### 1. 两套时钟体系（Hardware Tick vs OS Tick）
xv6-k210 原始代码针对真实 K210 开发板 (7.8MHz) 配置，在 QEMU 上每秒中断仅 4 次，导致时间系统严重失真。
* **解决方案**：在 `param.h` 中修改 `INTERVAL`，将硬件时钟频率对齐 QEMU (`10000000` Hz)，使操作系统 tick（时钟中断频率）提升至 `200Hz`。
* **分级计时**：`gettimeofday` 直接读取高精度的硬件寄存器 `r_time()`，而 `nanosleep` 则复用内核以系统 `ticks` 为单位的 `sleep` 机制。

### 2. `clone` 接口的陷阱帧 (Trapframe) 设置
最初测试 `clone` 时经常触发非法指令异常 (`scause 0x2`)，原因是 PC 指向了 0。
* **原因与修复**：不能简单复制父进程的 `epc`。测试程序在 `ecall` 前将线程入口函数 `fn` 和参数 `arg` 压入了新栈。内核必须从新栈指针 (`a1`) 处读取 `fn` 写入 `child_proc->trapframe->epc`，读取 `arg` 写入 `a0`，才能让新线程正确跳转执行。

### 3. POSIX 状态码格式 (`wait4`)
测试程序使用 `WEXITSTATUS(status)`（即右移 8 位）来提取子进程的退出码。因此，在内核 `wait` 循环中找到僵尸子进程时，必须**提前将状态码左移 8 位 (`np->xstate << 8`)** 再写回用户态地址，否则用户态提取出的状态码永远是 0。

## ✅ 测试情况

本地 `oscomp` 测试套件的 10 个测试点已全部完美通过：
- `test_wait`, `test_waitpid`
- `test_clone`, `test_fork`, `test_execve`, `test_exit`, `test_getppid`, `test_yield`
- `test_gettimeofday`, `test_sleep`