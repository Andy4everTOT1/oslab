#include "clock.h"
#include "riscv.h"
#include "sched.h"
#include "init.h"

void intr_enable(void) {
    // 设置 sstatus 的 SPIE 位为 1，启用中断
    unsigned long sstatus = read_csr(sstatus);
    sstatus |= (1UL << 5);  // SPIE 位
    sstatus |= (1UL << 1);  // SIE 位
    write_csr(sstatus, sstatus);
}

void intr_disable(void) {
    // 清除 sstatus 的 SIE 位，禁用中断
    unsigned long sstatus = read_csr(sstatus);
    sstatus &= ~(1UL << 1); // 清除 SIE 位
    write_csr(sstatus, sstatus);
}

void idt_init(void) {
  extern void trap_s(void);
  // 向 stvec 寄存器中写入中断处理后跳转函数的地址
  write_csr(stvec, trap_s);
}

void init(void) {
  idt_init();
  intr_enable();
  clock_init();
  task_init();
}
