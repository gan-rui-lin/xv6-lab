#include "types.h"
#include "memlayout.h"

// QEMU SiFive Test Finisher codes
#define TEST_FINISHER_FAIL    0x3333
#define TEST_FINISHER_PASS    0x5555
#define TEST_FINISHER_RESET   0x7777

// Minimal shutdown implementation for QEMU virt:
// If OpenSBI is present, the platform exposes a shutdown device (sifive_test).
// Writing PASS to TEST_DEVICE requests shutdown. This avoids linking against SBI libs.
void sbi_shutdown(void)
{
    // 使用 system reset extension
    register uint64 a0 asm("a0") = 0;          // reset type = shutdown
    register uint64 a1 asm("a1") = 0;          // reason = 0
    register uint64 a7 asm("a7") = 0x53525354; // SBI_EXT_SYSTEM_RESET
    register uint64 a6 asm("a6") = 0;          // extension id = 0
    asm volatile("ecall"
                 :
                 : "r"(a0), "r"(a1), "r"(a6), "r"(a7)
                 : "memory");
    __builtin_unreachable();
}
