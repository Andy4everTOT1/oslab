#ifndef PRINT_ONLY
#include "defs.h"
#include "clock.h"
#include "print.h"

void handler_s(uint64_t cause) {
  // interrupt
  if (cause & (1UL << 63)) {
    uint64_t interrupt_code = cause & 0xff;
    // supervisor timer interrupt
    if (interrupt_code == 5) {
      // 设置下一个时钟中断，打印当前的中断数目。
      ticks++;
      puts("Timer interrupt: ");
      put_num(ticks);
      puts("\n");
      clock_set_next_event();
    }
  }
}

#endif