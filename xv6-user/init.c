// init: The initial user-level program

#include "kernel/include/types.h"
#include "kernel/include/stat.h"
#include "kernel/include/file.h"
#include "kernel/include/fcntl.h"
#include "xv6-user/user.h"

// char *argv[] = { "sh", 0 };
char *argv[] = { 0 };
char* tests[] = {
  "/gettimeofday",
  "/sleep",        // 用户态叫 sleep，底层调 nanosleep
  "/clone",
  "/wait",
  "/waitpid",    
  "/yield",       
  "/getppid",
  "/fork",        // (自带)
  "/execve",      // (自带)
  "/exit",         // (自带)
  "getcwd",
  "write",
  "getpid",
  "times",
  "uname",
  "open",
  "openat",
  "brk",
  "mmap",
  "munmap"
};
int
main(void)
{
  int pid, wpid;

  // if(open("console", O_RDWR) < 0){
  //   mknod("console", CONSOLE, 0);
  //   open("console", O_RDWR);
  // }
  dev(O_RDWR, CONSOLE, 0);
  dup(0);  // stdout
  dup(0);  // stderr
  int length=sizeof(tests) / sizeof(tests[0]);
  for(int i=0; i<length; i++){
    printf("init: starting sh\n");
    pid = fork();
    if(pid < 0){
      printf("init: fork failed\n");
      exit(1);
    }
    if(pid == 0){
      char *t_argv[] = { tests[i] + 1, 0 };// 穿上正确的马甲
      exec(tests[i], t_argv);
      printf("init: exec sh failed\n");
      exit(1);
    }

    for(;;){
      // this call to wait() returns if the shell exits,
      // or if a parentless process exits.
      wpid = wait((int *) 0);
      if(wpid == pid){
        // the shell exited; restart it.
        break;
      } else if(wpid < 0){
        printf("init: wait returned an error\n");
        exit(1);
      } else {
        // it was a parentless process; do nothing.
      }
    }
  }

  // printf("\n[Debug] All tests finished. Ready to shutdown!\n"); // 加这句探针
  shutdown();
  return 0;
}
