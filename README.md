# xv6-k210 · Part 8：信号量机制

> **注意**：本仓库仅保留 `part8-sem` 分支（`branch 'part8-sem' set up to track 'github/fs'`），该分支包含信号量机制的完整实现，是本实验的核心交付物。

---

## 概述

本实验在 xv6-riscv 操作系统中实现**信号量（Semaphore）机制**，为用户态程序提供进程间同步与通信能力，解决自旋锁在复杂调度场景下的局限性。

xv6 原有的自旋锁仅适用于短临界区，忙等待会浪费 CPU，且无法高效解决需要长时间等待的经典并发问题。信号量基于 Dijkstra 的经典设计，以整型计数器加等待队列的方式支持两种原子操作：

- **P 操作（wait/down）**：计数器 > 0 则减 1 继续执行，否则阻塞当前进程。
- **V 操作（signal/up）**：计数器加 1，若有进程在等待则将其唤醒。

---

## 实现方案

### 数据结构

```c
// kernel/include/semaphore.h
#define NSEM 128

struct semaphore {
    struct spinlock lock;  // 保护信号量内部状态（P/V 时使用）
    int used;              // 是否已被分配
    int value;             // 当前计数值
};

static struct {
    struct spinlock lock;          // 保护槽位分配（create/destroy 时）
    struct semaphore sems[NSEM];   // 固定大小的信号量表，上限 128 个
} semtable;
```

**两层锁设计**：表锁只在 `sem_create`/`sem_destroy` 时持有，槽锁只在单个信号量的 P/V 操作时持有，不同信号量的操作互不干扰。

### P 操作（核心）

```c
while (s->value == 0) {
    if (!s->used) { release(&s->lock); return -1; }
    sleep(s, &s->lock);  // 原子释放锁并睡眠，唤醒后重新持有锁
}
s->value--;
```

使用 `while` 循环而非 `if`，遵循 **Mesa 语义**：被唤醒的进程重新竞争锁后须再次检查条件，防止虚假唤醒导致状态不一致。

`sleep(chan, lk)` 原子性地释放锁再进入睡眠，避免释放锁与睡眠之间的窗口期被 `wakeup` 穿越（**lost wakeup** 问题）。

### V 操作

计数器加 1 后调用 `wakeup`，唤醒所有在该信号量上睡眠的进程，由它们重新竞争。

### 销毁安全性

`sem_destroy` 在标记 `used=0` 前先调用 `wakeup`，确保所有阻塞在该信号量上的进程能被唤醒；它们在 `while` 循环中检测到 `used==0` 后返回错误，不会永久休眠。

---

## 新增 / 修改的文件

| 文件 | 操作 | 说明 |
|---|---|---|
| `kernel/include/semaphore.h` | 新建 | 信号量结构体定义与函数声明 |
| `kernel/semaphore.c` | 新建 | `seminit` / `sem_create` / `sem_destroy` / `sem_p` / `sem_v` 实现 |
| `kernel/main.c` | 修改 | `fileinit()` 后添加 `seminit()` 调用 |
| `kernel/sysproc.c` | 修改 | 四个系统调用包装函数 |
| `kernel/syscall.c` | 修改 | `extern` 声明、注册数组与名称数组 |
| `xv6-user/user.h` | 修改 | 用户态函数原型声明 |
| `xv6-user/usys.pl` | 修改 | 系统调用入口生成 |
| `Makefile` | 修改 | 将 `kernel/semaphore.o` 加入编译目标 |

---

## 测试验证

### 生产者-消费者（MPMC）

2 个生产者、2 个消费者，通过 pipe 通信，三个信号量协调：

| 信号量 | 初值 | 语义 |
|---|---|---|
| `mutex_sem` | 1 | 互斥锁，保护缓冲区访问 |
| `empty_sem` | 5 | 缓冲区空槽数，生产者生产前等待 |
| `full_sem` | 0 | 缓冲区已有数据数，消费者消费前等待 |

```
SUCCESS: All produced items were correctly consumed!
```

### 哲学家就餐（PHILOSOPHER）

5 位哲学家，每人就餐 2 次，每根筷子是一个初值为 1 的信号量。

**死锁避免策略**：偶数编号哲学家先拿左边筷子，奇数编号先拿右边筷子，打破资源申请的循环等待条件。

```
SUCCESS: All philosophers completed exactly 2 meals each!
```

### 测试结果

| 测试用例 | 结果 | 得分 |
|---|---|---|
| `test_ipc_producer_consumer` | ✅ 通过 | 1 / 1 |
| `test_ipc_philosopher` | ✅ 通过 | 1 / 1 |

---

## 运行环境

| 组件 | 说明 |
|---|---|
| 评测镜像 | `docker.educg.net/cg/os-contest:2024p6` |
| 运行平台 | QEMU virt 机器（10 MHz 硬件时钟） |
| 引导固件 | RustSBI QEMU v0.2.0-alpha.2 |
| 内核框架 | xv6-k210（RISC-V64，FAT32） |
| 测试套件 | oscomp/testsuits-for-oskernel（main 分支） |
| 构建工具 | riscv64 GCC 工具链 + GNU Make |

```bash
make all   # 编译，产物重命名为 kernel-qemu 和 sbi-qemu
make run   # 在 QEMU 上启动内核
```

> `Makefile` 中 `CPUS=1`，规避 RustSBI 多核启动时概率性卡死。容器内已是 root 权限，已删去 `sudo` 前缀。