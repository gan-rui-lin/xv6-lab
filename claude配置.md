## 当你被要求写调试/适配文档

+ 如果是调试任务，请首先给出调试的罪魁祸首，并结合具体 debug 输出进行深入且细致的分析，要有逻辑性（为什么从这个输出判断出来有问题）
+ 如果是适配任务，请首先给出适配的思路、架构等；针对简化实现的部分、及花了很长时间去适配的部分也要做重点讲述

均不应该少于 1500 字；均可以稍微介绍对应的背景知识

默认情况下应该放在 /home/grl/codeRepo/rcore-lab/docs/GRLDocs 路径下，并且新开一个 `.md` 文档。文档名应该具有概括性。需要在文档里面说明日期(如 2026/2/18)。

## 检查 log 文件

+ 你应该主要关注 `signum = 4`（IllegalInstruction）和 `signum = 12`（StorePageFault）, `trap_handler: Exception`,`[ERROR]`/`[WARN]`,`bad addr` `SIGKILL` `Panicked`这类关键日志；如果有 `sepc` 和 `stval` 相关日志也要重点分析。当然, syscall 的 **负返回值** 也很重要。
+ 如何检验测试是否成功：如果日志中没有上述关键日志，并且输出 `test sbrk`,`test clone`等测试用例的正常输出（例如 `unlink sucucess`），则**才可以**认为测试成功了。 由于 `rcore-lab` 不会轻易 `panic`，所以执行到 `=== All tests completed ===` 并不意味着测试成功了。
+ 你可以使用 `rg`（ripgrep）工具来搜索 log 文件中的关键信息，例如：

  - `rg "IllegalInstruction" all*.log` 搜索所有日志中出现 IllegalInstruction 的行。

## 对应的测试源码

对应的 `sdcard-rv.img` 一般挂载上 `/mnt/sdcard-2025` 路径上，`initcode.rs` 所指的 `busybox` 可执行文件在 `/mnt/sdcard-2025/musl/busybox` 而不是 `/home/grl/codeRepo/rcore-lab/busybox/musl/busybox`。

如果 `/mnt/sdcard-2025` 文件夹为空，则需要手动挂载，挂载命令为：

```bash
sudo mount -o loop sdcard-rv.img /mnt/sdcard-2025
```

[busybox 源码路径](../../testsuits-for-oskernel/busybox)

## 当你被要求调试-测试循环

你可以通过 `warn!`,`trace!`,`info!`,`error!`等日志输出函数来输出调试信息；然后执行 `LOG=TRACE bash run.sh -f sdcard-rv.img -t all > all{num}.log` 来获取日志输出（但是往往会导致**日志量非常巨大**）；其中 `num` 是一个递增的数字，代表第几轮调试；你可以通过 `rg` 来搜索日志中的关键信息；如果你认为问题已经解决了，可以执行 `LOG=TRACE bash run.sh -f sdcard-rv.img -t all > all{num}.log` 来验证测试是否成功了。

如果你觉得日志行数太多，可以执行 `LOG=ERROR bash run.sh -f sdcard-rv.img -t all > all{num}.log`来只输出错误日志 或者执行 `LOG=INFO bash run.sh -f sdcard-rv.img -t all > all{num}.log`来输出错误和警告日志。
