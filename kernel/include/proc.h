#ifndef __PROC_H
#define __PROC_H

#include "param.h"
#include "riscv.h"
#include "types.h"
#include "spinlock.h"
#include "file.h"
#include "fat32.h"
#include "trap.h"

#define MAX_VMA 16  // 每个进程最多16个映射区域

// 页面状态
#define VPAGE_UNUSED  0   // 从未分配
#define VPAGE_INMEM   1   // 在内存中
#define VPAGE_SWAPPED 2   // 已换出

// 单个mmap页面的追踪信息
struct VMA_page {
  int    status;         // 页面状态
  uint64 vaddr;          // 该页虚拟地址
  int    swap_slot_idx;  // 换出时对应的slot编号，-1表示无
  uint64 last_in_mem_time;   // 进入内存的时间（FIFO用）
  uint64 last_access_time;   // 最后访问时间（LRU用）
};

struct VMA {
  uint64 vm_start;
  uint64 vm_end;
  int prot;
  int flags;
  uint64 vm_off;
  struct VMA_page pages[10]; // 最多追踪10个页面
  struct VMA *vm_next, *vm_prev;
};

// Saved registers for kernel context switches.
struct context {
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

// Per-CPU state.
struct cpu {
  struct proc *proc;          // The process running on this cpu, or null.
  struct context context;     // swtch() here to enter scheduler().
  int noff;                   // Depth of push_off() nesting.
  int intena;                 // Were interrupts enabled before push_off()?
};

extern struct cpu cpus[NCPU];

enum procstate { UNUSED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
struct proc {
  struct spinlock lock;

  // p->lock must be held when using these:
  enum procstate state;        // Process state
  struct proc *parent;         // Parent process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  int xstate;                  // Exit status to be returned to parent's wait
  int pid;                     // Process ID

  // these are private to the process, so p->lock need not be held.
  uint64 kstack;               // Virtual address of kernel stack
  uint64 sz;                   // Size of process memory (bytes)
  pagetable_t pagetable;       // User page table
  pagetable_t kpagetable;      // Kernel page table
  struct trapframe *trapframe; // data page for trampoline.S
  struct context context;      // swtch() here to run process
  struct file *ofile[NOFILE];  // Open files
  struct dirent *cwd;          // Current directory
  char name[16];               // Process name (debugging)
  int tmask;
  struct VMA head;
  // Part6 新增
  int max_page_in_mem;     // mmap区允许的最大驻留页数
  int cur_page_in_mem;     // 当前实际驻留页数
  int page_swap_count;     // 累计换出次数
};

void            reg_info(void);
int             cpuid(void);
void            exit(int);
int             fork(void);
int             growproc(int);
pagetable_t     proc_pagetable(struct proc *);
void            proc_freepagetable(pagetable_t, uint64);
int             kill(int);
struct cpu*     mycpu(void);
struct cpu*     getmycpu(void);
struct proc*    myproc();
void            procinit(void);
void            scheduler(void) __attribute__((noreturn));
void            sched(void);
void            setproc(struct proc*);
void            sleep(void*, struct spinlock*);
void            userinit(void);
int             wait(uint64);
void            wakeup(void*);
void            yield(void);
int             either_copyout(int user_dst, uint64 dst, void *src, uint64 len);
int             either_copyin(void *dst, int user_src, uint64 src, uint64 len);
void            procdump(void);
uint64          procnum(void);
void            test_proc_init(int);
struct VMA*     allocshare();
void            freeshare(struct VMA *vma);

#endif