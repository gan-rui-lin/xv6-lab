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



