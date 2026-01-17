1. 为什么有的时候打印会串行？
   
   疑似是 smp=2 下多核竞争打印设备导致的，但未深入验证。

2. FAT32 代码已经是一坨勾史了，能不能重构一下？

   目前优先级不高，后续有时间会考虑。

3. sys_mount 相关的 TODO？

   目前暂时搁置，后续会考虑实现更完整的挂载/卸载功能。

4. time 相关的时间计时不太准确，要调整。

5. FAT32 相关 log 打印会触发内存访问异常。

6. opensbi 有时无法进入入口点 0x80200000，怀疑是 qemu 与 opensbi 版本兼容性问题。

   OpenSBI v1.0
____ _____ ____ _____
/ __ \ / ____| _ \_ _|
| | | |_ __ ___ _ __ | (___ | |_) || |
| | | | '_ \ / _ \ '_ \ \___ \| _ < | |
| |__| | |_) | __/ | | |____) | |_) || |_
\____/| .__/ \___|_| |_|_____/|____/_____|
| |
|_|

Platform Name : riscv-virtio,qemu
Platform Features : medeleg
Platform HART Count : 2
Platform IPI Device : aclint-mswi
Platform Timer Device : aclint-mtimer @ 10000000Hz
Platform Console Device : uart8250
Platform HSM Device : ---
Platform Reboot Device : sifive_test
Platform Shutdown Device : sifive_test
Firmware Base : 0x80000000
Firmware Size : 260 KB
Runtime SBI Version : 0.3

Domain0 Name : root
Domain0 Boot HART : 1
Domain0 HARTs : 0*,1*
Domain0 Region00 : 0x0000000002000000-0x000000000200ffff (I)
Domain0 Region01 : 0x0000000080000000-0x000000008007ffff ()
Domain0 Region02 : 0x0000000000000000-0xffffffffffffffff (R,W,X)
Domain0 Next Address : 0x0000000080200000
Domain0 Next Arg1 : 0x0000000087000000
Domain0 Next Mode : S-mode
Domain0 SysReset : yes

Boot HART ID : 1
Boot HART Domain : root
Boot HART ISA : rv64imafdcsuh
Boot HART Features : scounteren,mcounteren,time
Boot HART PMP Count : 16
Boot HART PMP Granularity : 4
Boot HART PMP Address Bits: 54
Boot HART MHPM Count : 0
Boot HART MIDELEG : 0x0000000000001666
Boot HART MEDELEG : 0x0000000000f0b509

7. 逐步移除 xv6 相关代码，专注 riscv64 平台。（实际上已经有很多部分不再支持 xv6）

8. 2026-01-17：ppoll/sendfile 目前为简化实现，仅满足 busybox 基本运行，需要后续完善。
