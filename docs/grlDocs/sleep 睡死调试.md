# sleep 睡死调试

sleep 的库实现实际就是调用 `sys_nanosleep`，该系统调用在内核中实现睡眠功能。本文记录一次 sleep 调用后进程“睡死”无法唤醒的调试过程。

`sys_nanosleep` 的实现位于 `src/syscall/systime.c`，其核心逻辑如下：



```c
uint64
sys_nanosleep(void)
{
  // args: a0 = req (user ptr), a1 = rem (user ptr)
  uint64 req_addr, rem_addr;
  argaddr(0, &req_addr);
  argaddr(1, &rem_addr);

  struct {
    long sec;
    long usec;
  } req;

  if (req_addr == 0)
    return -1;
  if (copyin(myproc()->pagetable, (char*)&req, req_addr, sizeof(req)) != 0)
    return -1;

  if (req.sec < 0 || req.usec < 0 || req.usec >= 1000000L)
    return -1;

  // 精确转换为 tick 数：假定时钟中断为 1ms/tick（HZ=1000）。
  // 将秒/微秒转换为总纳秒并按 1ms 进行向上取整。
  const long TICK_NS = 1000000L;   // 1 ms
  unsigned long long total_ns = (unsigned long long)req.sec * 1000000000ULL
                              + (unsigned long long)req.usec * 1000ULL;
  int n_ticks = (int)((total_ns + TICK_NS - 1) / TICK_NS); // ceil(total_ns / 1ms)

  if (n_ticks <= 0)
    return 0;

  int ret = sleep_ticks(n_ticks);

  // 完成后，若需要，置剩余时间为0
  if (rem_addr) {
    struct { long sec; long usec; } rem = {0, 0};
    if (copyout(myproc()->pagetable, rem_addr, (char*)&rem, sizeof(rem)) < 0)
      return -1;
  }
  return ret;
}
```

从代码可见，`sys_nanosleep` 会将请求的秒和微秒转换为内核“tick”数，然后调用 `sleep_ticks(n_ticks)` 进行实际睡眠。

`tick` 是内核的时间单位，通常由时钟中断驱动。假设系统配置为每毫秒一个 tick（HZ=1000），则 `sleep_ticks` 会让进程睡眠指定的 tick 数。

打印日志发现，`clockintr()` 中的 tick 计数没有递增，猜测对应的异常号没有正确处理，导致时钟中断没有被响应。

xv6 的 2019 版中，对应 RISC-V 的时钟中断处理在 `src/kernel/trap.c` 中：

```c
 else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.
    // 通过机器级的时钟中断(timervec)触发的 S 级的软件中断(sip[1] = 0)

    // 只在 CPU0 下处理时钟中断
    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } 
```

但是由于大赛要求使用 sbi 模式运行 xv6，因此时钟中断的处理逻辑有所不同。sbi 模式下，时钟中断会触发机器级中断（M-mode），然后通过 SBI 调用转发到 S-mode。

修改代码如下：

```c
else if((scause & 0x8000000000000000L) && (scause & 0xff) == 5){
    // supervisor timer interrupt (STIP), typically provided via SBI/aclint-mtimer
    if(cpuid() == 0){
      clockintr();
    }

    // acknowledge by clearing the STIP bit in sip
    w_sip(r_sip() & ~(1 << 5));

    return 2;
  } 
```

通过 sbi 模式触发时钟中断的流程大致如下：

```c
void sbi_set_timer(uint64_t stime)
{
    register uint64_t a0 asm("a0") = stime;
    register uint64_t a6 asm("a6") = 0;          // FID = 0 (SBI_EXT_TIME_SET_TIMER)
    register uint64_t a7 asm("a7") = 0x54494D45; // EID = 0x54494D45 (SBI_EXT_TIME)
    
    asm volatile("ecall"
                 : "+r"(a0) 
                 : "r"(a6), "r"(a7)
                 : "memory");
}


```


OpenSBI提供的sbi_set_timer()接口，可以传入一个时刻，让它在那个时刻触发一次时钟中断。内核在每次处理完时钟中断后，需要重新设置下一个时钟中断的触发时间。

```c

// Choose a tick interval in cycles. From OpenSBI info: mtimer @ 10MHz.
// 1ms tick -> 10,000 cycles.
#define TICK_CYCLES 10000ULL

void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);

  // Program first timer interrupt via SBI (legacy). This kicks off ticking.
  sbi_set_timer(r_time() + TICK_CYCLES);
}

```

```c

void
clockintr()
{
  acquire(&tickslock);
  // log_debug("clockintr: ticks before=%d\n", ticks);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);

  // Schedule next tick.
  sbi_set_timer(r_time() + TICK_CYCLES);
}
```

然后 sys_nanosleep 调用就能正确被唤醒，进程不会再“睡死”了。