//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//


#include "include/types.h"
#include "include/riscv.h"
#include "include/param.h"
#include "include/stat.h"
#include "include/spinlock.h"
#include "include/proc.h"
#include "include/sleeplock.h"
#include "include/file.h"
#include "include/pipe.h"
#include "include/fcntl.h"
#include "include/fat32.h"
#include "include/syscall.h"
#include "include/string.h"

#include "include/printf.h"
#include "include/vm.h"
struct mount mounts[NMOUNT];

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if(argint(n, &fd) < 0)
    return -1;
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == NULL)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

static struct dirent*
create(char *path, short type, int mode)
{
  struct dirent *ep, *dp;
  char name[FAT32_MAX_FILENAME + 1];

  if((dp = enameparent(path, name)) == NULL)
    return NULL;

  if (type == T_DIR) {
    mode = ATTR_DIRECTORY;
  } else if (mode & O_RDONLY) {
    mode = ATTR_READ_ONLY;
  } else {
    mode = 0;  
  }

  elock(dp);
  if ((ep = ealloc(dp, name, mode)) == NULL) {
    eunlock(dp);
    eput(dp);
    return NULL;
  }
  
  if ((type == T_DIR && !(ep->attribute & ATTR_DIRECTORY)) ||
      (type == T_FILE && (ep->attribute & ATTR_DIRECTORY))) {
    eunlock(dp);
    eput(ep);
    eput(dp);
    return NULL;
  }

  eunlock(dp);
  eput(dp);

  elock(ep);
  return ep;
}

uint64
sys_open(void)
{
  char path[FAT32_MAX_PATH];
  int fd, omode;
  struct file *f;
  struct dirent *ep;

  if(argstr(0, path, FAT32_MAX_PATH) < 0 || argint(1, &omode) < 0)
    return -1;

  if(omode & O_CREATE){
    ep = create(path, T_FILE, omode);
    if(ep == NULL){
      return -1;
    }
  } else {
    if((ep = ename(path)) == NULL){
      return -1;
    }
    elock(ep);
    if((ep->attribute & ATTR_DIRECTORY) && omode != O_RDONLY){
      eunlock(ep);
      eput(ep);
      return -1;
    }
  }

  if((f = filealloc()) == NULL || (fd = fdalloc(f)) < 0){
    if (f) {
      fileclose(f);
    }
    eunlock(ep);
    eput(ep);
    return -1;
  }

  if(!(ep->attribute & ATTR_DIRECTORY) && (omode & O_TRUNC)){
    etrunc(ep);
  }

  f->type = FD_ENTRY;
  f->off = (omode & O_APPEND) ? ep->file_size : 0;
  f->ep = ep;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  eunlock(ep);

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[FAT32_MAX_PATH];
  struct dirent *ep;

  if(argstr(0, path, FAT32_MAX_PATH) < 0 || (ep = create(path, T_DIR, 0)) == 0){
    return -1;
  }
  eunlock(ep);
  eput(ep);
  return 0;
}

uint64
sys_chdir(void)
{
  char path[FAT32_MAX_PATH];
  struct dirent *ep;
  struct proc *p = myproc();
  
  if(argstr(0, path, FAT32_MAX_PATH) < 0 || (ep = ename(path)) == NULL){
    return -1;
  }
  elock(ep);
  if(!(ep->attribute & ATTR_DIRECTORY)){
    eunlock(ep);
    eput(ep);
    return -1;
  }
  eunlock(ep);
  eput(p->cwd);
  p->cwd = ep;
  return 0;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  // if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
  //    copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
  if(copyout2(fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout2(fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

// To open console device.
uint64
sys_dev(void)
{
  int fd, omode;
  int major, minor;
  struct file *f;

  if(argint(0, &omode) < 0 || argint(1, &major) < 0 || argint(2, &minor) < 0){
    return -1;
  }

  if(omode & O_CREATE){
    panic("dev file on FAT");
  }

  if(major < 0 || major >= NDEV)
    return -1;

  if((f = filealloc()) == NULL || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    return -1;
  }

  f->type = FD_DEVICE;
  f->off = 0;
  f->ep = 0;
  f->major = major;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  return fd;
}

// To support ls command
uint64
sys_readdir(void)
{
  struct file *f;
  uint64 p;

  if(argfd(0, 0, &f) < 0 || argaddr(1, &p) < 0)
    return -1;
  return dirnext(f, p);
}

// get absolute cwd string
uint64
sys_getcwd(void)
{
  uint64 addr;
  int size; 


  if (argaddr(0, &addr) < 0 || argint(1, &size) < 0)
    return 0; 

  struct dirent *de = myproc()->cwd;
  char path[FAT32_MAX_PATH];
  char *s;
  int len;

  if (de->parent == NULL) {
    s = "/";
  } else {
    s = path + FAT32_MAX_PATH - 1;
    *s = '\0';
    while (de->parent) {
      len = strlen(de->filename);
      s -= len;
      if (s <= path)          // can't reach root "/"
        return 0; // 
      strncpy(s, de->filename, len);
      *--s = '/';
      de = de->parent;
    }
  }

  // 
  if (strlen(s) + 1 > size) {
    return 0; // 
  }

  // 
  // if (copyout(myproc()->pagetable, addr, s, strlen(s) + 1) < 0)
  if (copyout2(addr, s, strlen(s) + 1) < 0)
    return 0; // 
  
  // 
  return addr;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct dirent *dp)
{
  struct dirent ep;
  int count;
  int ret;
  ep.valid = 0;
  ret = enext(dp, &ep, 2 * 32, &count);   // skip the "." and ".."
  return ret == -1;
}

uint64
sys_remove(void)
{
  char path[FAT32_MAX_PATH];
  struct dirent *ep;
  int len;
  if((len = argstr(0, path, FAT32_MAX_PATH)) <= 0)
    return -1;

  char *s = path + len - 1;
  while (s >= path && *s == '/') {
    s--;
  }
  if (s >= path && *s == '.' && (s == path || *--s == '/')) {
    return -1;
  }
  
  if((ep = ename(path)) == NULL){
    return -1;
  }
  elock(ep);
  if((ep->attribute & ATTR_DIRECTORY) && !isdirempty(ep)){
      eunlock(ep);
      eput(ep);
      return -1;
  }
  elock(ep->parent);      // Will this lead to deadlock?
  eremove(ep);
  eunlock(ep->parent);
  eunlock(ep);
  eput(ep);

  return 0;
}

// Must hold too many locks at a time! It's possible to raise a deadlock.
// Because this op takes some steps, we can't promise
uint64
sys_rename(void)
{
  char old[FAT32_MAX_PATH], new[FAT32_MAX_PATH];
  if (argstr(0, old, FAT32_MAX_PATH) < 0 || argstr(1, new, FAT32_MAX_PATH) < 0) {
      return -1;
  }

  struct dirent *src = NULL, *dst = NULL, *pdst = NULL;
  int srclock = 0;
  char *name;
  if ((src = ename(old)) == NULL || (pdst = enameparent(new, old)) == NULL
      || (name = formatname(old)) == NULL) {
    goto fail;          // src doesn't exist || dst parent doesn't exist || illegal new name
  }
  for (struct dirent *ep = pdst; ep != NULL; ep = ep->parent) {
    if (ep == src) {    // In what universe can we move a directory into its child?
      goto fail;
    }
  }

  uint off;
  elock(src);     // must hold child's lock before acquiring parent's, because we do so in other similar cases
  srclock = 1;
  elock(pdst);
  dst = dirlookup(pdst, name, &off);
  if (dst != NULL) {
    eunlock(pdst);
    if (src == dst) {
      goto fail;
    } else if (src->attribute & dst->attribute & ATTR_DIRECTORY) {
      elock(dst);
      if (!isdirempty(dst)) {    // it's ok to overwrite an empty dir
        eunlock(dst);
        goto fail;
      }
      elock(pdst);
    } else {                    // src is not a dir || dst exists and is not an dir
      goto fail;
    }
  }

  if (dst) {
    eremove(dst);
    eunlock(dst);
  }
  memmove(src->filename, name, FAT32_MAX_FILENAME);
  emake(pdst, src, off);
  if (src->parent != pdst) {
    eunlock(pdst);
    elock(src->parent);
  }
  eremove(src);
  eunlock(src->parent);
  struct dirent *psrc = src->parent;  // src must not be root, or it won't pass the for-loop test
  src->parent = edup(pdst);
  src->off = off;
  src->valid = 1;
  eunlock(src);

  eput(psrc);
  if (dst) {
    eput(dst);
  }
  eput(pdst);
  eput(src);

  return 0;

fail:
  if (srclock)
    eunlock(src);
  if (dst)
    eput(dst);
  if (pdst)
    eput(pdst);
  if (src)
    eput(src);
  return -1;
}



/**
 * @brief 迭代法获取目录绝对路径
 * 将 dirent 节点一层层上溯记录到数组中，最后正向拼接出完整路径。
 */
static int build_abs_path(struct dirent* node, char* buffer, int max_len) {
    if (node == NULL) return -1;
    struct dirent* path_stack[32]; // 假设最大目录深度为 32
    int depth = 0;
    struct dirent* curr = node;
    
    // 迭代向上追溯到根目录
    while (curr->parent != NULL) {
        if (depth >= 32) return -1; // 路径过深
        path_stack[depth++] = curr;
        curr = curr->parent;
    }
    
    // 初始化根目录 "/"
    if (max_len < 2) return -1;
    buffer[0] = '/';
    buffer[1] = '\0';
    int current_len = 1;

    // 倒序遍历数组，正向拼接路径
    for (int i = depth - 1; i >= 0; i--) {
        int name_len = strlen(path_stack[i]->filename);
        if (current_len + name_len + 1 >= max_len) return -1;
        
        safestrcpy(buffer + current_len, path_stack[i]->filename, max_len - current_len);
        current_len += name_len;
        
        // 如果不是最后一级，加斜杠
        if (i > 0) {
            buffer[current_len++] = '/';
            buffer[current_len] = '\0';
        }
    }
    return 0;
}

/**
 * @brief 路径解析入口
 */
int resolve_path(int dirfd, char* usr_path) {
    if (!usr_path) return -1;
    if (usr_path[0] == '/') return 0; // 绝对路径
    // 剥离开头的 "./"
    char* rel_path = usr_path;
    if (rel_path[0] == '.' && rel_path[1] == '/') {
        rel_path += 2;
    }

    struct proc* p = myproc();
    struct dirent* base_dir = NULL;

    // 确定基准目录
    if (dirfd == AT_FDCWD) {
        base_dir = p->cwd;
    } else {
        if (dirfd < 0 || dirfd >= NOFILE) return -1;
        struct file* base_f = p->ofile[dirfd];
        // 确保 fd 有效且是个目录
        if (base_f == NULL || !(base_f->ep->attribute & ATTR_DIRECTORY)) return -1;
        base_dir = base_f->ep;
    }

    // 组装最终绝对路径
    char base_buf[FAT32_MAX_PATH];
    if (build_abs_path(base_dir, base_buf, FAT32_MAX_PATH) < 0) return -1;

    char final_result[FAT32_MAX_PATH];
    safestrcpy(final_result, base_buf, sizeof(final_result));
    int baselen = strlen(final_result);

    // 拼接基准路径与相对路径
    if (baselen > 1) { // 如果基准路径不仅仅是根目录 "/"
        if (baselen + 1 >= sizeof(final_result)) return -1;
        final_result[baselen++] = '/';
        final_result[baselen] = '\0';
    }
    safestrcpy(final_result + baselen, rel_path, sizeof(final_result) - baselen);
    
    // 写回原地址
    safestrcpy(usr_path, final_result, FAT32_MAX_PATH);
    return 0;
}
/**
 * @brief sys_openat 实现
 */
uint64 sys_openat(void) {
    char target_path[FAT32_MAX_PATH];
    int dfd, file_flags, open_mode, out_fd;
    struct file* new_file;
    struct dirent* node;

    // 1. 获取用户态参数
    if (argint(0, &dfd) < 0 ||
        argstr(1, target_path, FAT32_MAX_PATH) < 0 || //防止传入字符过长
        argint(2, &file_flags) < 0 ||
        argint(3, &open_mode) < 0) {
        return -1;
    }

    if (target_path[0] == '\0') return -1;

    // 2. 将相对路径转化为绝对路径
    if (resolve_path(dfd, target_path) < 0) {
        return -1;
    }

    // 3. 寻找或创建文件节点
    if (file_flags & O_CREATE) {
        node = create(target_path, T_FILE, open_mode);
        if (node == NULL) return -1;
    } else {
        node = ename(target_path);
        if (node == NULL) return -1;
        elock(node);

        // 修复判断 Bug：严格检查目录写权限
        int is_dir = (node->attribute & ATTR_DIRECTORY);
        int write_intent = (file_flags & (O_WRONLY | O_RDWR));
        
        if (is_dir && write_intent) {
            eunlock(node);
            eput(node);
            return -1;
        }
    }

    // 4. 为进程分配 file 结构和描述符
    new_file = filealloc();
    if (new_file == NULL) {
        eunlock(node);
        eput(node);
        return -1;
    }
    
    out_fd = fdalloc(new_file);
    if (out_fd < 0) {
        fileclose(new_file); // fileclose 内部处理回收
        eunlock(node);
        eput(node);
        return -1;
    }

    // 5. 截断文件（如果是 O_TRUNC 且非目录）
    if (!(node->attribute & ATTR_DIRECTORY) && (file_flags & O_TRUNC)) {
        etrunc(node);
    }

    // 6. 初始化打开的文件属性
    new_file->type = FD_ENTRY;
    new_file->ep = node;
    new_file->off = (file_flags & O_APPEND) ? node->file_size : 0;
    
    // 权限位设置
    new_file->readable = !(file_flags & O_WRONLY);
    new_file->writable = (file_flags & O_WRONLY) || (file_flags & O_RDWR);

    eunlock(node);
    return out_fd;
}
uint64
sys_dup3(void)
{
  int ofd, nfd;
  struct file *f;
  struct proc *p = myproc();

  if(argint(0, &ofd) < 0 || argfd(0, 0, &f) < 0)
    return -1;
  if(argint(1, &nfd) < 0)
    return -1;
  if(nfd < 0 || nfd >= NOFILE || nfd == ofd)
    return -1;

  if(p->ofile[nfd])
    fileclose(p->ofile[nfd]);

  filedup(f);
  p->ofile[nfd] = f;
  return nfd;
}

uint64
sys_pipe2(void)
{
  uint64 fdarray;
  int flags;
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if(argaddr(0, &fdarray) < 0 || argint(1, &flags) < 0)
    return -1;
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(copyout2(fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout2(fdarray+sizeof(fd0), (char*)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}
uint64
sys_getdents(void)
{
  int fd, len;
  uint64 addr;
  struct file *f;
  int nread = 0;
  int reclen = (int)sizeof(struct dirent64);

  if(argfd(0, &fd, &f) < 0 || argaddr(1, &addr) < 0 || argint(2, &len) < 0)
    return -1;

  if(len < reclen)
    return 0;

  if(f->readable == 0)
    return -1;

  if(f->ep == 0 || !(f->ep->attribute & ATTR_DIRECTORY))
    return -1;

  while(nread + reclen <= len){
    struct dirent de;
    int count = 0;
    int ret;

    elock(f->ep);
    ret = enext(f->ep, &de, f->off, &count);
    eunlock(f->ep);

    if(ret == -1)
      break;

    f->off += count * 32;

    struct dirent64 out;
    out.d_ino = 0;
    out.d_off = f->off;
    out.d_reclen = sizeof(struct dirent64);
    out.d_type = (de.attribute & ATTR_DIRECTORY) ? DT_DIR : DT_REG;
    safestrcpy(out.d_name, de.filename, FAT32_MAX_FILENAME + 1);

    if(copyout2(addr, (char*)&out, sizeof(out)) < 0)
      return -1;

    addr += sizeof(out);
    nread += sizeof(out);
  }

  return nread;
}
uint64
sys_mkdirat(void)
{
  char path[FAT32_MAX_PATH];
  int dirfd, mode;
  struct dirent *ep;

  if(argint(0, &dirfd) < 0 ||
     argstr(1, path, FAT32_MAX_PATH) < 0 ||
     argint(2, &mode) < 0)
    return -1;

  if(strlen(path) == 0)
    return -1;

  if(resolve_path(dirfd, path) < 0)
    return -1;

  ep = create(path, T_DIR, 0);
  if(ep == 0)
    return -1;

  eunlock(ep);
  eput(ep);
  return 0;
}
uint64
sys_unlinkat(void)
{
  char path[FAT32_MAX_PATH];
  int dirfd, flags;
  struct dirent *ep;

  if(argint(0, &dirfd) < 0 ||
     argstr(1, path, FAT32_MAX_PATH) < 0 ||
     argint(2, &flags) < 0)
    return -1;

  if(strlen(path) == 0)
    return -1;

  if(resolve_path(dirfd, path) < 0)
    return -1;

  // 找到路径最后一段，禁止删除 . 和 ..
  char *base = path;
  for(char *q = path; *q; q++)
    if(*q == '/') base = q + 1;
  if(strncmp(base, ".", 2) == 0 || strncmp(base, "..", 3) == 0)
    return -1;

  ep = ename(path);
  if(ep == 0)
    return -1;

  elock(ep);

  if(ep->attribute & ATTR_DIRECTORY){
    if(!(flags & AT_REMOVEDIR)){
      eunlock(ep);
      eput(ep);
      return -1;
    }
    if(!isdirempty(ep)){
      eunlock(ep);
      eput(ep);
      return -1;
    }
  } else {
    if(flags & AT_REMOVEDIR){
      eunlock(ep);
      eput(ep);
      return -1;
    }
  }

  elock(ep->parent);
  eremove(ep);
  eunlock(ep->parent);
  eunlock(ep);
  eput(ep);
  return 0;
}
uint64
sys_mount(void)
{
  // char src[FAT32_MAX_PATH];
  // char dst[FAT32_MAX_PATH];
  // char fstype[32];
  // int flags;
  // uint64 data;

  // if(argstr(0, src, FAT32_MAX_PATH) < 0 ||
  //    argstr(1, dst, FAT32_MAX_PATH) < 0 ||
  //    argstr(2, fstype, sizeof(fstype)) < 0 ||
  //    argint(3, &flags) < 0 ||
  //    argaddr(4, &data) < 0)
  //   return -1;

  // if(resolve_path(AT_FDCWD, dst) < 0)
  //   return -1;

  // struct dirent *ep = ename(dst);
  // if(ep == 0)
  //   return -1;

  // elock(ep);
  // if(!(ep->attribute & ATTR_DIRECTORY)){
  //   eunlock(ep);
  //   eput(ep);
  //   return -1;
  // }

  // int idx = -1;
  // for(int i = 0; i < NMOUNT; i++){
  //   if(!mounts[i].used){
  //     idx = i;
  //     break;
  //   }
  // }
  // if(idx == -1){
  //   eunlock(ep);
  //   eput(ep);
  //   return -1;
  // }

  // mounts[idx].de = edup(ep);
  // mounts[idx].used = 1;
  // safestrcpy(mounts[idx].path, dst, FAT32_MAX_PATH);
  // eunlock(ep);
  // eput(ep);
  return 0;
}

uint64
sys_umount(void)
{
  // char path[FAT32_MAX_PATH];
  // int flags;

  // if(argstr(0, path, FAT32_MAX_PATH) < 0 ||
  //    argint(1, &flags) < 0)
  //   return -1;

  // if(resolve_path(AT_FDCWD, path) < 0)
  //   return -1;

  // int idx = find_mount(path);
  // if(idx < 0)
  //   return -1;

  // if(mounts[idx].de)
  //   eput(mounts[idx].de);

  // mounts[idx].de = 0;
  // mounts[idx].used = 0;
  // safestrcpy(mounts[idx].path, "", FAT32_MAX_PATH);
  return 0;
}