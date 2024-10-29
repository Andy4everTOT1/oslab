#ifndef PRINT_ONLY
#include "defs.h"
#include "riscv.h"
#include "clock.h"
#include "print.h"
volatile unsigned long long ticks = 0;

// 需要自行修改 timebase 使得时钟中断的间隔恰好为 1s
static uint64_t timebase = 1e7;

// 使用 rdtime 汇编指令获得当前 mtime 中的值并返回
// 你并不需要修改该函数
uint64_t get_cycles(void) {
#if __riscv_xlen == 64
  uint64_t n;
  __asm__ __volatile__("rdtime %0" : "=r"(n));
  return n;
#else
  uint32_t lo, hi, tmp;
  __asm__ __volatile__(
      "1:\n"
      "rdtimeh %0\n"
      "rdtime %1\n"
      "rdtimeh %2\n"
      "bne %0, %2, 1b"
      : "=&r"(hi), "=&r"(lo), "=&r"(tmp));
  return ((uint64_t)hi << 32) | lo;
#endif
}

void clock_init(void) {
  puts("ZJU OS LAB 2       Student_ID:3220106416\n");

  // 对 sie 寄存器中的时钟中断位设置（ sie[stie] = 1 ）以启用时钟中断。
  set_csr(sie, 1 << 5);  // 启用 Supervisor Timer 中断 (stie = 1)
  // 设置第一个时钟中断。
  clock_set_next_event();
}


void clock_set_next_event(void) {
  // 获取当前 cpu cycles 数并计算下一个时钟中断的发生时刻
  // 通过调用 OpenSBI 提供的函数设置下一次的时钟中断时间
  uint64_t current_time = get_cycles();
  uint64_t next_time = current_time + timebase;
  sbi_call(0x00, 0x00, next_time, 0, 0, 0, 0, 0);
}


#endif