// Timer Interrupt handler


#include "include/types.h"
#include "include/param.h"
#include "include/riscv.h"
#include "include/sbi.h"
#include "include/spinlock.h"
#include "include/timer.h"
#include "include/printf.h"
#include "include/proc.h"



#include "include/syscall.h"
#include "include/kalloc.h"
#include "include/string.h"
#include "include/vm.h"

struct spinlock tickslock;
uint ticks;

void timerinit() {
    initlock(&tickslock, "time");
    #ifdef DEBUG
    printf("timerinit\n");
    #endif
}

void
set_next_timeout() {
    // There is a very strange bug,
    // if comment the `printf` line below
    // the timer will not work.

    // this bug seems to disappear automatically
    // printf("");
    sbi_set_timer(r_time() + INTERVAL);
}

void timer_tick() {
    acquire(&tickslock);
    ticks++;
    wakeup(&ticks);
    release(&tickslock);
    set_next_timeout();
}

int get_and_copyout(uint64 arg_index, char* src, uint64 size) {
  uint64 dest_addr;
  
  // Fetch the destination address from the user registers
  if (argaddr(arg_index, &dest_addr) < 0) {
    return -1;
  }
  
  // Copy the data from kernel space to user space
  if (copyout2(dest_addr, src, size) < 0) {
    return -1;
  }
  
  return 0;
}


uint64 sys_times(void) {
  struct tms tms;
  

  // Acquire the lock to safely read the global variable 'ticks'
  acquire(&tickslock);
  
  
  // Assign the ticks to the tms structure.
  // (Note: In a complete OS, utime/stime would be process-specific statistics, 
  // but this is a common simplified implementation for this lab).
  tms.tms_utime  = ticks;
  tms.tms_stime  = ticks;
  tms.tms_cutime = ticks;
  tms.tms_cstime = ticks;
  release(&tickslock);

  // Use the helper function to copy the struct to user space (argument 0)
  if (get_and_copyout(0, (char *)&tms, sizeof(tms)) < 0) {
    return -1;
  }

  // Return the elapsed ticks since boot upon success
  return ticks;
}
