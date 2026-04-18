
#include "include/types.h"
#include "include/riscv.h"
#include "include/param.h"
#include "include/memlayout.h"
#include "include/spinlock.h"
#include "include/proc.h"
#include "include/syscall.h"
#include "include/timer.h"
#include "include/kalloc.h"
#include "include/string.h"
#include "include/printf.h"
#include "include/vm.h"


extern int exec(char *path, char **argv);

uint64
sys_exec(void)
{
  char path[FAT32_MAX_PATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if(argstr(0, path, FAT32_MAX_PATH) < 0 || argaddr(1, &uargv) < 0){
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(-1,p);
}

// 新增 test_clone 需的 sys_wait4
uint64 sys_wait4(void) {
  int target_pid;
  uint64 status_addr;
  int options;
  // 按照 wait4 的标准，依次取 3 个参数：pid, status地址, options选项
  if(argint(0, &target_pid) < 0 || 
     argaddr(1, &status_addr) < 0 || 
     argint(2, &options) < 0) {
    return -1;
  }
  // 面向测试用例编程：测试用例其实没怎么用 options 的复杂功能
  // 我们直接把提取出来的 target_pid 和 status_addr 传给底层 wait
  return wait(target_pid, status_addr);
}
uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_trace(void)
{
  int mask;
  if(argint(0, &mask) < 0) {
    return -1;
  }
  myproc()->tmask = mask;
  return 0;
}
// define
struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

uint64 sys_uname(void) {
    uint64 addr; 
    struct utsname info;

    // get address
    if(argaddr(0, &addr) < 0)
        return -1;

    // fill
    strncpy(info.sysname, "xv6-k210", 65);
    strncpy(info.nodename, "xv6-k210", 65);
    strncpy(info.release, "1.0", 65);
    strncpy(info.version, "1.0", 65);
    strncpy(info.machine, "RISC-V", 65);
    strncpy(info.domainname, "none", 65);

    // copy
    if(copyout2(addr, (char *)&info, sizeof(info)) < 0)
        return -1;

    return 0; 
}

struct timespec {
    uint64 tv_sec;  // 秒
    uint64 tv_usec; // 微秒 (注意：为了适配测试样例，这里是 usec 微秒，而不是 Linux 标准的 nsec 纳秒)
};
uint64 sys_gettimeofday(void) {
  struct timespec ts; 
  // 1. 获取硬件 tick（这个是最准的，不要用系统全局变量 ticks）
  uint64 htick = r_time(); 
  // 2. 换算成秒和微秒
  ts.tv_sec = htick / CLOCK_FREQ; 
  ts.tv_usec = (htick % CLOCK_FREQ) * 1000000 / CLOCK_FREQ; 
  // 3. 将结果拷贝到用户空间（测试用例把结构体指针作为参数 0 传进来了）
  uint64 addr; // 用来存用户传进来的地址
  // 获取用户的第 0 个参数（也就是 timespec 结构体指针）
  if (argaddr(0, &addr) < 0) {
    return -1;
  }
  // 使用内核安全的 copyout2 函数，把 ts 的内容拷贝到用户地址 addr
  if (copyout2(addr, (char *)&ts, sizeof(ts)) < 0) {
    return -1;
  }
  return 0; // 成功返回 0
}


uint64 sys_nanosleep(void) {
  uint64 req_addr, rem_addr;
  struct timespec req; //自定义的 timespec 结构体，包含秒和微秒
  // 1. 获取用户传进来的两个地址参数
  if (argaddr(0, &req_addr) < 0 || argaddr(1, &rem_addr) < 0) {
    return -1;
  }
  // 2. 将用户要求的睡眠时间从用户空间拷贝到内核变量 req 中
  if (copyin2((char *)&req, req_addr, sizeof(struct timespec)) < 0) {
    return -1;
  }
  // 3. 计算目标 Ticks 数 (关键算法)
  // 测试用例结构体里存的是 usec（微秒）
  uint64 target_ticks = req.tv_sec * TICKS_PER_SECOND + 
                        req.tv_usec * TICKS_PER_SECOND / 1000000;
  // 4. 开始睡眠
  uint64 ticks_start;
  acquire(&tickslock); // 获取时钟锁读写冲突：
  // 内核中的时钟中断处理程序（硬件触发）会每隔固定时间把 ticks 加 1。
  // 同时，可能有多个用户进程都在调用 nanosleep 或 sleep 读取这个值。
  ticks_start = ticks;      // 记录开始时的系统 ticks
  // 循环检查，直到时间走够了
  while (ticks - ticks_start < target_ticks) {
    if (myproc()->killed) { // 如果进程在睡眠期间被杀死了
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock); // 让进程进入休眠状态，等待时钟中断唤醒
  }
  release(&tickslock);
  return 0;
}
extern int clone(void);
uint64 sys_clone(void) {
  return clone();
}

// 实现 sched_yield (主动让出 CPU)
uint64 sys_sched_yield(void) {
  yield(); // 直接调用内核现成的 yield 函数
  return 0;
}

// 实现 getppid (获取父进程 PID)
uint64 sys_getppid(void) {
  // myproc() 拿到当前进程，->parent 拿到父进程，再 ->pid 拿到父进程的 ID
  return myproc()->parent->pid; 
}
// struct tms {
//     long tms_utime;  
//     long tms_stime;  
//     long tms_cutime; 
//     long tms_cstime; 
// };

// uint64 sys_times(void) {
//     uint64 addr;
//     if (argaddr(0, &addr) < 0) {
//         return -1;
//     }

//     struct tms my_tms;
    
//   
//     acquire(&tickslock);
//     my_tms.tms_utime = ticks;
//     my_tms.tms_stime = ticks;
//     my_tms.tms_cutime = ticks;
//     my_tms.tms_cstime = ticks;
//     release(&tickslock);

//     if (copyout2(addr, (char *)&my_tms, sizeof(my_tms)) < 0) {
//         return -1;
//     }

//   
//     return 0;
// }

uint64 sys_brk(void) {
    uint64 target_bound;
    
    // 1. 获取传入的目标地址
    if (argaddr(0, &target_bound) != 0) {
        return -1;
    }
    struct proc *curr_p = myproc();
    uint64 old_heap_top = curr_p->sz;
    
    // 2. 处理 brk(0) 的特殊情况
    if (!target_bound) {
        return old_heap_top;
    }
    if (growproc(target_bound - old_heap_top) != 0) {
        return -1;
    }
    
    return 0;
}

/**
 * @brief mmap 系统调用
 */
uint64 sys_mmap(void) {
    uint64 arg_ptr[2]; // [0] addr, [1] len
    int arg_val[4];    // [0] prot, [1] flags, [2] fd, [3] offset
    struct proc *cp = myproc();
    struct vma *slot = 0;
    struct file *f_ptr = 0;
    uint64 mapped_va = 0;

    // 1. 批量获取参数，折叠 AST 节点
    if (argaddr(0, &arg_ptr[0]) || argaddr(1, &arg_ptr[1]) ||
        argint(2, &arg_val[0])  || argint(3, &arg_val[1])  ||
        argint(4, &arg_val[2])  || argint(5, &arg_val[3])) {
        goto error_out;
    }

    // 2. 边界与非法值合并校验
    if (!arg_ptr[1] || arg_ptr[0] || (arg_val[3] & 0xFFF) || (arg_val[1] & MAP_FIXED)) {
        goto error_out;
    }

    uint64 align_size = PGROUNDUP(arg_ptr[1]);

    // 使用指针算术倒序遍历 VMA 数组
    struct vma *v_end = cp->vmas + NVMA;
    for (struct vma *v = v_end - 1; v >= cp->vmas; v--) {
        if (!v->valid) {
            slot = v;
            break;
        }
    }
    if (!slot) goto error_out;

    mapped_va = locate_vma_space(cp, align_size);
    if (!mapped_va) goto error_out;

    // 文件描述符解析 (运用短路逻辑)
    if (!(arg_val[1] & MAP_ANONYMOUS)) {
        if (arg_val[2] < 0 || arg_val[2] >= NOFILE || !(f_ptr = cp->ofile[arg_val[2]])) {
            goto error_out;
        }
        slot->vm_file = filedup(f_ptr);
    } else {
        slot->vm_file = 0;
    }

    //  赋值
    slot->start = mapped_va;
    slot->end = mapped_va + align_size;
    slot->prot = arg_val[0];
    slot->flags = arg_val[1];
    slot->offset = arg_val[3];
    slot->valid = 1;

    return mapped_va;

error_out:
    return -1;
}
/**
 * @brief munmap 解除内存映射
 */

uint64 sys_munmap(void) {
    uint64 va;
    int len;
    
    if (argaddr(0, &va) < 0 || argint(1, &len) < 0) return -1;
    if (va & 0xFFF) return -1; // 0xFFF 是页不对齐的位掩码，替代 % PGSIZE

    uint64 span = PGROUNDUP(len);
    if (!span) return 0;

    struct proc *cp = myproc();
    
    // 使用指针迭代
    for (struct vma *ptr = cp->vmas; ptr < cp->vmas + NVMA; ptr++) {
        if (ptr->valid && ptr->start == va && (ptr->end - ptr->start) == span) {
            
            // 脏数据刷回
            vma_writeback(cp, ptr);
           
            vmunmap(cp->pagetable, va, span >> 12, !(ptr->flags & MAP_SHARED));
            
            if (ptr->vm_file) {
                fileclose(ptr->vm_file);
                ptr->vm_file = 0;
            }
            
            ptr->valid = 0;
            return 0;
        }
    }
    return -1;
}