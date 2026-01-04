执行：
```sh
make all && qemu-system-riscv64 -machine virt \
  -kernel kernel-qemu \
  -m 128M -nographic -smp 2 \
  -bios /home/grl/codeRepo/opensbi/build/platform/generic/firmware/fw_jump.bin \
  -drive file=sdcard.img,if=none,format=raw,id=x0 \
  -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
  -device virtio-net-device,netdev=net \
  -netdev user,id=net -s -S
```

可以看到 OpenSBI 的启动日志，如下所示：

```
OpenSBI v1.0
   ____                    _____ ____ _____
  / __ \                  / ____|  _ \_   _|
 | |  | |_ __   ___ _ __ | (___ | |_) || |
 | |  | | '_ \ / _ \ '_ \ \___ \|  _ < | |
 | |__| | |_) |  __/ | | |____) | |_) || |_
  \____/| .__/ \___|_| |_|_____/|____/_____|
        | |
        |_|

Platform Name             : riscv-virtio,qemu
Platform Features         : medeleg
Platform HART Count       : 2
Platform IPI Device       : aclint-mswi
Platform Timer Device     : aclint-mtimer @ 10000000Hz
Platform Console Device   : uart8250
Platform HSM Device       : ---
Platform Reboot Device    : sifive_test
Platform Shutdown Device  : sifive_test
Firmware Base             : 0x80000000
Firmware Size             : 252 KB
Runtime SBI Version       : 0.3

Domain0 Name              : root
Domain0 Boot HART         : 1
Domain0 HARTs             : 0*,1*
Domain0 Region00          : 0x0000000002000000-0x000000000200ffff (I)
Domain0 Region01          : 0x0000000080000000-0x000000008003ffff ()
Domain0 Region02          : 0x0000000000000000-0xffffffffffffffff (R,W,X)
Domain0 Next Address      : 0x0000000080200000
Domain0 Next Arg1         : 0x0000000082200000
Domain0 Next Mode         : S-mode
Domain0 SysReset          : yes

Boot HART ID              : 1
Boot HART Domain          : root
Boot HART ISA             : rv64imafdcsuh
Boot HART Features        : scounteren,mcounteren,time
Boot HART PMP Count       : 16
Boot HART PMP Granularity : 4
Boot HART PMP Address Bits: 54
Boot HART MHPM Count      : 0
Boot HART MIDELEG         : 0x0000000000001666
Boot HART MEDELEG         : 0x0000000000f0b509
```

另一个窗口运行 `gdb-multiarch`，连接到 QEMU：

```sh
gdb-multiarch kernel-qemu -ex "target remote :1234"
```

运行：

```gdb
 xv6-lab git:(simple-doc) ✗ gdb-multiarch kernel-qemu -ex "target remote:1234"
GNU gdb (Ubuntu 15.0.50.20240403-0ubuntu1) 15.0.50.20240403-git
Copyright (C) 2024 Free Software Foundation, Inc.
License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>
This is free software: you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.
Type "show copying" and "show warranty" for details.
This GDB was configured as "x86_64-linux-gnu".
Type "show configuration" for configuration details.
For bug reporting instructions, please see:
<https://www.gnu.org/software/gdb/bugs/>.
Find the GDB manual and other documentation resources online at:
    <http://www.gnu.org/software/gdb/documentation/>.

For help, type "help".
Type "apropos word" to search for commands related to "word"...
Reading symbols from kernel-qemu...
Remote debugging using :1234
0x0000000000001000 in ?? ()
(gdb) c
Continuing.
^C
Thread 1 received signal SIGINT, Interrupt.
0x0000000080009b8c in ?? ()
(gdb) p /x $mcause
$1 = 0x0
(gdb) p /x $scause
$2 = 0x0
(gdb) x /100i $pc
=> 0x80009b8c:  mv      a0,s1
   0x80009b8e:  jal     0x800047c6
   0x80009b92:  bne     a0,s2,0x80009b88
   0x80009b96:  csrw    mie,s3
   0x80009b9a:  j       0x80009b62
   0x80009b9c:  li      a0,-1006
   0x80009ba0:  j       0x80009b64
   0x80009ba2:  addi    sp,sp,-32
   0x80009ba4:  sd      s0,16(sp)
   0x80009ba6:  sd      ra,24(sp)
   0x80009ba8:  sd      s1,8(sp)
   0x80009baa:  addi    s0,sp,32
   0x80009bac:  mv      a5,a0
   0x80009bae:  auipc   a0,0xe
   0x80009bb2:  ld      a0,1290(a0)
   0x80009bb6:  add     a0,a0,a5
   0x80009bb8:  li      a2,1
   0x80009bba:  li      a1,3
   0x80009bbc:  ld      s1,40(a5)
   0x80009bbe:  jal     0x80004816
   0x80009bc2:  sext.w  a0,a0
   0x80009bc4:  li      a5,3
   0x80009bc6:  bne     a0,a5,0x80009bde
--Type <RET> for more, q to quit, c to continue without paging--c
   0x80009bca:  auipc   a5,0xe
   0x80009bce:  ld      a5,1270(a5)
   0x80009bd2:  beqz    a5,0x80009bf4
   0x80009bd4:  ld      a4,32(a5)
   0x80009bd6:  beqz    a4,0x80009bf4
   0x80009bd8:  ld      a5,40(a5)
   0x80009bda:  beqz    a5,0x80009bf4
   0x80009bdc:  jalr    a5
   0x80009bde:  csrr    a1,mhartid
   0x80009be2:  auipc   a0,0xc
   0x80009be6:  addi    a0,a0,-250
   0x80009bea:  sext.w  a1,a1
   0x80009bec:  jal     0x80005676
   0x80009bf0:  jal     0x8000983c
   0x80009bf4:  jalr    s1
   0x80009bf6:  j       0x80009bde
   0x80009bf8:  addi    sp,sp,-64
   0x80009bfa:  sd      s0,48(sp)
   0x80009bfc:  sd      s3,24(sp)
   0x80009bfe:  sd      ra,56(sp)
   0x80009c00:  sd      s1,40(sp)
   0x80009c02:  sd      s2,32(sp)
   0x80009c04:  sd      s4,16(sp)
   0x80009c06:  sd      s5,8(sp)
   0x80009c08:  sd      s6,0(sp)
   0x80009c0a:  addi    s0,sp,64
   0x80009c0c:  mv      s3,a4
   0x80009c0e:  li      a4,1
   0x80009c10:  bltu    a4,s3,0x80009cbc
   0x80009c14:  mv      s6,a0
   0x80009c16:  mv      s2,a1
   0x80009c18:  mv      s1,a2
   0x80009c1a:  mv      s4,a3
   0x80009c1c:  mv      s5,a5
   0x80009c1e:  beqz    a1,0x80009c38
   0x80009c20:  mv      a1,a2
   0x80009c22:  mv      a0,s2
   0x80009c24:  jal     0x800057e4
   0x80009c28:  beqz    a0,0x80009cbc
   0x80009c2a:  li      a3,4
   0x80009c2c:  mv      a2,s3
   0x80009c2e:  mv      a1,s4
   0x80009c30:  mv      a0,s2
   0x80009c32:  jal     0x800058ee
   0x80009c36:  beqz    a0,0x80009cc4
   0x80009c38:  slli    a5,s1,0x20
   0x80009c3c:  srli    a4,a5,0x1d
   0x80009c40:  auipc   a5,0x2d
   0x80009c44:  addi    a5,a5,-224
   0x80009c48:  add     a5,a5,a4
   0x80009c4a:  ld      s2,0(a5)
   0x80009c4e:  beqz    s2,0x80009cbc
   0x80009c52:  auipc   a0,0xe
   0x80009c56:  ld      a0,1126(a0)
   0x80009c5a:  li      a2,2
   0x80009c5c:  li      a1,1
   0x80009c5e:  add     a0,a0,s2
   0x80009c60:  jal     0x80004816
   0x80009c64:  sext.w  a4,a0
   0x80009c68:  beqz    a4,0x80009cc0
   0x80009c6a:  li      a5,1
   0x80009c6c:  bne     a4,a5,0x80009cbc
   0x80009c70:  mv      a0,s1
   0x80009c72:  jal     0x80000d90
   0x80009c76:  auipc   a5,0xe
   0x80009c7a:  ld      a5,1098(a5)
   0x80009c7e:  sd      s5,16(s2)
   0x80009c82:  sd      s4,24(s2)
   0x80009c86:  sd      s3,32(s2)
   0x80009c8a:  beqz    a5,0x80009ca0
   0x80009c8c:  ld      a4,32(a5)
   0x80009c8e:  beqz    a4,0x80009ca0
   0x80009c90:  ld      a5,40(a5)
   0x80009c92:  beqz    a5,0x80009c9e
   0x80009c94:  ld      a1,40(s6)
   0x80009c98:  mv      a0,s1
   0x80009c9a:  jalr    a4
(gdb) si
0x0000000080009b8e in ?? ()
(gdb)  display /i $pc
1: x/i $pc
=> 0x80009b8e:  jal     0x800047c6
(gdb) si
0x00000000800047c6 in ?? ()
1: x/i $pc
=> 0x800047c6:  addi    sp,sp,-16
(gdb) si
0x00000000800047c8 in ?? ()
1: x/i $pc
=> 0x800047c8:  sd      s0,8(sp)
(gdb) 
0x00000000800047ca in ?? ()
1: x/i $pc
=> 0x800047ca:  addi    s0,sp,16
(gdb) 
0x00000000800047cc in ?? ()
1: x/i $pc
=> 0x800047cc:  ld      a0,0(a0)
(gdb) 
0x00000000800047ce in ?? ()
1: x/i $pc
=> 0x800047ce:  fence   ir,ir
(gdb) 
0x00000000800047d2 in ?? ()
1: x/i $pc
=> 0x800047d2:  ld      s0,8(sp)
(gdb) 
0x00000000800047d4 in ?? ()
1: x/i $pc
=> 0x800047d4:  addi    sp,sp,16
(gdb) 
0x00000000800047d6 in ?? ()
1: x/i $pc
=> 0x800047d6:  ret
(gdb) 
0x0000000080009b92 in ?? ()
1: x/i $pc
=> 0x80009b92:  bne     a0,s2,0x80009b88
(gdb) 
0x0000000080009b88 in ?? ()
1: x/i $pc
=> 0x80009b88:  wfi
(gdb) 
0x0000000080009b8c in ?? ()
1: x/i $pc
=> 0x80009b8c:  mv      a0,s1
(gdb) 
0x0000000080009b8e in ?? ()
1: x/i $pc
=> 0x80009b8e:  jal     0x800047c6
(gdb) 
0x00000000800047c6 in ?? ()
1: x/i $pc
=> 0x800047c6:  addi    sp,sp,-16
(gdb) 
0x00000000800047c8 in ?? ()
1: x/i $pc
=> 0x800047c8:  sd      s0,8(sp)
(gdb) 
0x00000000800047ca in ?? ()
1: x/i $pc
=> 0x800047ca:  addi    s0,sp,16
(gdb) 
0x00000000800047cc in ?? ()
1: x/i $pc
=> 0x800047cc:  ld      a0,0(a0)
(gdb) 
0x00000000800047ce in ?? ()
1: x/i $pc
=> 0x800047ce:  fence   ir,ir
(gdb) 
0x00000000800047d2 in ?? ()
1: x/i $pc
=> 0x800047d2:  ld      s0,8(sp)
(gdb) 
0x00000000800047d4 in ?? ()
1: x/i $pc
=> 0x800047d4:  addi    sp,sp,16
(gdb) 
0x00000000800047d6 in ?? ()
1: x/i $pc
=> 0x800047d6:  ret
(gdb) 
0x0000000080009b92 in ?? ()
1: x/i $pc
=> 0x80009b92:  bne     a0,s2,0x80009b88
(gdb) 
0x0000000080009b88 in ?? ()
1: x/i $pc
=> 0x80009b88:  wfi
(gdb) 
0x0000000080009b8c in ?? ()
1: x/i $pc
=> 0x80009b8c:  mv      a0,s1
(gdb) thread 1
[Switching to thread 1 (Thread 1.1)]
#0  0x0000000080009b8c in ?? ()
(gdb) thread 2
[Switching to thread 2 (Thread 1.2)]
#0  main () at src/boot/main.c:55
55              while (started == 0)
(gdb) 
```

通过单步调试可以看到，hart 0 停在了 `wfi` 指令处，而没有正确跳转到 `_entry` 入口点 `0x80200000`，怀疑是 OpenSBI 与 QEMU 版本兼容性问题导致的。

而且这个 BUG 并不是每次都会出现，**有时可以**正常进入入口点。