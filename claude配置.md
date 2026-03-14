
你可以通过 `warn!`,`trace!`,`info!`,`error!`等日志输出函数来输出调试信息；然后执行 `LOG=SYSCALLbash run.sh -f sdcard-rv.img -t all > all_rv{num}.log` 来获取日志输出；其中 `num` 是一个递增的数字，代表第几轮调试；你可以通过 `rg` 来搜索日志中的关键信息；如果你认为问题已经解决了，可以执行 `LOG=OFF bash run.sh -f sdcard-rv.img -t all > all{num}.log` 来验证测试是否成功了。

如果你觉得日志行数太多，可以执行 `LOG=ERROR bash run.sh -f sdcard-rv.img -t all > all_rv{num}.log`来只输出错误日志 或者执行 `LOG=INFO bash run.sh -f sdcard-rv.img -t all > all_rv{num}.log`来输出错误和警告日志。

➜  rcore-lab git:(muti-arch) ✗ qemu-system-riscv64 --version
QEMU emulator version 9.2.1
Copyright (c) 2003-2024 Fabrice Bellard and the QEMU Project developers
➜  rcore-lab git:(muti-arch) ✗
