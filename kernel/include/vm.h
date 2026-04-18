#ifndef __VM_H 
#define __VM_H 

#include "types.h"
#include "riscv.h"
// 定义单进程最大的 VMA 数量
#define NVMA 16
struct proc;
struct file;
// 定义 mmap 的权限标志位 (对齐 Linux 标准)
#define PROT_READ       (1 << 0)
#define PROT_WRITE      (1 << 1)
#define PROT_EXEC       (1 << 2)

// 定义 mmap 的映射模式
#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_FIXED       0x10
#define MAP_ANONYMOUS   0x20

// 核心结构体：VMA
struct vma {
    int valid;              // 1 表示这块区域被占用了，0 表示空闲
    uint64 start;           // 起始虚拟地址
    uint64 end;             // 结束虚拟地址
    int prot;               // 权限
    int flags;              // 映射标志
    struct file* vm_file;   // 绑定的文件 (匿名映射为 0/NULL)
    uint64 offset;          // 文件偏移量
};

void            kvminit(void);
void            kvminithart(void);
uint64          kvmpa(uint64);
void            kvmmap(uint64, uint64, uint64, int);
int             mappages(pagetable_t, uint64, uint64, uint64, int);
pagetable_t     uvmcreate(void);
// void            uvminit(pagetable_t, uchar *, uint);
void            uvminit(pagetable_t, pagetable_t, uchar *, uint);
uint64          uvmalloc(pagetable_t, pagetable_t, uint64, uint64);
uint64          uvmdealloc(pagetable_t, pagetable_t, uint64, uint64);
// int             uvmcopy(pagetable_t, pagetable_t, uint64);
int             uvmcopy(pagetable_t, pagetable_t, pagetable_t, uint64);
void            uvmfree(pagetable_t, uint64);
// void            uvmunmap(pagetable_t, uint64, uint64, int);
void            vmunmap(pagetable_t, uint64, uint64, int);
void            uvmclear(pagetable_t, uint64);
uint64          walkaddr(pagetable_t, uint64);
int             copyout(pagetable_t, uint64, char *, uint64);
int             copyin(pagetable_t, char *, uint64, uint64);
int             copyinstr(pagetable_t, char *, uint64, uint64);
pagetable_t     proc_kpagetable(void);
void            kvmfreeusr(pagetable_t kpt);
void            kvmfree(pagetable_t kpagetable, int stack_free);
uint64          kwalkaddr(pagetable_t pagetable, uint64 va);
int             copyout2(uint64 dstva, char *src, uint64 len);
int             copyin2(char *dst, uint64 srcva, uint64 len);
int             copyinstr2(char *dst, uint64 srcva, uint64 max);
void            vmprint(pagetable_t pagetable);
uint64          locate_vma_space(struct proc* current_p, uint64 need_len);
void            vma_writeback(struct proc* p, struct vma* v);
void            vma_free(struct proc* p);
#endif 
