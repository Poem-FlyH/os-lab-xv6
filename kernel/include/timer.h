#ifndef __TIMER_H
#define __TIMER_H

#include "types.h"
#include "spinlock.h"

extern struct spinlock tickslock;
extern uint ticks;

void timerinit();
void set_next_timeout();
void timer_tick();
struct tms {
    long tms_utime; //user time
    long tms_stime; // system time
    long tms_cutime; // child user time
    long tms_cstime; //child system time
};
#endif
