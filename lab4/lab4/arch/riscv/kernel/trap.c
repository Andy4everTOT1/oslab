#include "defs.h"
#include "sched.h"
#include "test.h"
#include "syscall.h"
#include "stdio.h"

void handler_s(uint64_t cause, uint64_t epc, uint64_t sp) {
  // interrupt
  if (cause >> 63 == 1) {
    // supervisor timer interrupt
    if (cause == 0x8000000000000005) {
      asm volatile("ecall");
      ticks++;
      if (ticks % 10 == 0) {
        do_timer();
      }
    }
  }
  // exception
  else if (cause >> 63 == 0) {
    // instruction page fault
    if (cause == 0xc) {
      printf("Instruction page fault! epc = 0x%016lx\n", epc);
      while (1);
    }
    // load page fault
    else if (cause == 0xd) {
      printf("Load page fault! epc = 0x%016lx\n", epc);
      while (1);
    }
    // Store/AMO page fault
    else if (cause == 0xf) {
      printf("Store/AMO page fault! epc = 0x%016lx\n", epc);
      while (1);
    }
    // syscall from user mode
    else if (cause == 0x8) {
      // DONE: 根据我们规定的接口规范，从a7中读取系统调用号，然后从a0~a5读取参数，调用对应的系统调用处理函数，最后把返回值保存在a0~a1中。
      //       注意读取和修改的应该是保存在栈上的值，而不是寄存器中的值，因为寄存器上的值可能被更改。

      // 1. 从 a7 中读取系统调用号
      // 2. 从 a0 ~ a5 中读取系统调用参数
      // 2. 调用syscall()，并把返回值保存到 a0,a1 中
      // 3. sepc += 4，注意应该修改栈上的sepc，而不是sepc寄存器
      
      // 提示，可以用(uint64_t*)(sp)得到一个数组
      
      struct stack {
          uint64_t ra;
          uint64_t t0;
          uint64_t t1;
          uint64_t t2;
          uint64_t a0;
          uint64_t a1;
          uint64_t a2;
          uint64_t a3;
          uint64_t a4;
          uint64_t a5;
          uint64_t a6;
          uint64_t a7;
          uint64_t t3;
          uint64_t t4;
          uint64_t t5;
          uint64_t t6;
          uint64_t sepc;
      };
      struct stack *stack_pointer = (struct stack *)sp;
      struct ret_info ret = syscall(
        stack_pointer->a7,
        stack_pointer->a0,
        stack_pointer->a1,
        stack_pointer->a2,
        stack_pointer->a3,
        stack_pointer->a4,
        stack_pointer->a5
      );
      stack_pointer->a0 = ret.a0;
      stack_pointer->a1 = ret.a1;
      stack_pointer->sepc += 4;
    }
    else {
      printf("Unknown exception! epc = 0x%016lx\n", epc);
      while (1);
    }
  }
  return;
}
