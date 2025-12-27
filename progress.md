# Branch
```cmd
(zjy) zjy@LAPTOP-M2TJSAKR:~/workspace/projects/xv6-lab$  git branch -a
  ext4
  fs-xv6
* fs-xv6-adp
  master
  zjy
  remotes/origin/HEAD -> origin/master
  remotes/origin/docs
  remotes/origin/ext4
  remotes/origin/fs-xv6
  remotes/origin/grl
  remotes/origin/master
  remotes/origin/proc_fix
  remotes/origin/process
  remotes/origin/spinlock
  remotes/origin/syscall-simple
  remotes/origin/traps
  ```

  # Command
```
make clean && make fat32-gdb

gdb-multiarch kernel/kernel

target remote :1234
  ```

# 进度
当前已经集成，但是在proc.c:228 usertrapret报错

12/27 3:30
测试结果
测试样例名	通过测试点	全部测试点
test_execve	3	3
test_open	3	3
test_getdents	0	5
test_gettimeofday	3	3
test_munmap	0	4
test_yield	4	4
test_getpid	3	3
test_mount	0	5
test_dup	2	2
test_waitpid	4	4
test_write	0	2
test_close	0	2
test_exit	2	2
test_times	0	6
test_read	3	3
test_getppid	2	2
test_clone	4	4
test_openat	4	4
test_mmap	0	3
test_fork	3	3
test_sleep	0	2
test_mkdir	0	3
test_umount	0	5
test_chdir	0	3
test_unlink	0	2
test_fstat	2	3
test_pipe	4	4
test_getcwd	0	2
test_dup2	0	2
test_brk	3	3
test_uname	0	2
test_wait	4	4

sleep 应该比较简单，其它几个不好说