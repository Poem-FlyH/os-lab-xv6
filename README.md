# xv6-k210 · Part 7：文件系统系统调用

## 概述

本分支在 xv6-k210（RISC-V64，FAT32 文件系统）上实现了一批 POSIX 文件操作系统调用，目标是通过 oscomp 官方测试套件的文件系统相关测试点。

xv6-k210 以 FAT32 替代了原始 xv6 的类 Unix 简单文件系统，目录项由 `struct dirent` 管理，功能类似 Linux 的 inode。

---

## 已实现的系统调用

| 系统调用 | 调用号 | 对应测试点 | 状态 |
|---|---|---|---|
| `dup3` | 24 | `test_dup2` | ✅ 通过 |
| `pipe2` | 59 | `test_pipe` | ✅ 通过 |
| `getdents64` | 61 | `test_getdents` | ✅ 通过 |
| `mkdirat` | 34 | `test_mkdir` | ✅ 通过 |
| `unlinkat` | 35 | `test_unlink` | ✅ 通过 |
| `fstat` | 80 | `test_fstat` | ✅ 通过 |
| `mount` | 40 | `test_mount` | ❌ 未通过（见下） |
| `umount2` | 39 | `test_umount` | ❌ 未通过（见下） |

---

## 实现说明

### `dup3`

`dup3` 是 `dup2` 的扩展，允许用户指定目标 fd 编号，若该 fd 已被占用则先关闭。

同时将 `NOFILE`（进程最大打开文件数）从 16 扩大至 **256**，以支持测试用例中使用大 fd 号（如 `fd=100`）的场景。

```c
if (p->ofile[nfd]) fileclose(p->ofile[nfd]);
filedup(f);
p->ofile[nfd] = f;
return nfd;
```

### `pipe2`

`pipe2` 在 `pipe` 基础上增加了 `flags` 参数（如 `O_CLOEXEC`）。由于 xv6 不支持这些标志位，`flags` 在实现中被忽略，核心逻辑与 `sys_pipe` 相同。

### `getdents64`

读取目录项，是 `ls` 等命令的底层基础。依赖 FAT32 的 `enext` 函数遍历目录，将内核 `dirent` 格式转换为用户态的 `dirent64` 格式后写入用户空间。

```c
struct dirent64 {
    uint64        d_ino;      // FAT32 无 inode，填 0
    uint64        d_off;      // 下一个目录项的偏移
    unsigned short d_reclen;  // 本结构体大小
    unsigned char  d_type;    // DT_DIR=4 或 DT_REG=8
    char           d_name[256];
};
```

### `mkdirat`

在指定目录下创建子目录。通过 `resolve_path` 处理 `dirfd`（支持 `AT_FDCWD` 与具体 fd），转换为绝对路径后调用 `create`。

### `unlinkat`

删除文件或目录。通过 `flags` 中的 `AT_REMOVEDIR` 标志区分操作对象，禁止删除 `.` 和 `..`，删除目录时检查是否为空（`isdirempty`），最终调用 `eremove`。

```c
if (ep->attribute & ATTR_DIRECTORY) {
    if (!(flags & AT_REMOVEDIR)) return -1;  // 目标是目录但未指定 AT_REMOVEDIR
    if (!isdirempty(ep))         return -1;  // 目录非空，拒绝删除
} else {
    if (flags & AT_REMOVEDIR)    return -1;  // 目标是文件但指定了 AT_REMOVEDIR
}
elock(ep->parent);
eremove(ep);
eunlock(ep->parent);
```

### `fstat`

获取文件状态信息。新增 `kstat` 结构体对齐 Linux `stat` 接口，并新增 `ekstat` 辅助函数填充字段。`st_mode` 根据目录属性判断文件类型，`st_blocks` 按 `(st_size + 511) / 512` 向上取整。

---

## 重要辅助修改

- **`NOFILE` 扩大至 256**：`kernel/include/param.h`，支持大 fd 号测试场景。
- **`isdirempty` 函数**：新增于 `kernel/fat32.c`，通过 `enext` 跳过 `.` 和 `..` 后检查目录是否含有效条目，用于 `unlinkat` 的安全检查。
- **未知系统调用返回 0**：`kernel/syscall.c` 中将未注册调用的返回值从 `-1` 改为 `0`，减少 libc 初始化失败的概率。

---



## mount / umount 未通过的原因

调试发现预编译测试二进制在进入 `main` 之前就发生缺页异常——va 的值恰好是字符串 `"mount"` 的 ASCII 编码，说明程序从未正确执行。

根本原因：测试程序基于标准 libc 编译，libc 初始化阶段会调用 xv6 不支持的系统调用（如调用号 1062），导致初始化失败后跳转到错误地址。

内核侧工作已完成（`sysnum.h` 定义调用号、`syscall.c` 注册、`sys_mount`/`sys_umount` 返回 0），但因上述兼容性问题测试无法进入内核。远程评测环境同样未通过，推测原因一致。

**后续方向**：补齐 libc 初始化所依赖的系统调用（分析 strace 或反汇编确认调用号列表），或完善内核对未知调用的容错，使测试程序能正常进入 `main`。

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
make all        # 编译，产物重命名为 kernel-qemu 和 sbi-qemu
make run        # 在 QEMU 上启动内核
```

> 注意：`Makefile` 中 `CPUS=1`（规避 RustSBI 多核启动时概率性卡死），容器内已是 root 权限，已删去 `sudo` 前缀。