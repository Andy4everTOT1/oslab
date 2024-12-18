#include "defs.h"
#include "sched.h"
#include "test.h"
#include "syscall.h"
#include "stdio.h"
#include "task_manager.h"
#include "mm.h"
#include "vm.h"

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
    if (cause == 0xc || cause == 0xd || cause == 0xf) {
      uint64_t stval;
      uint64_t* sp_ptr = (uint64_t*)(sp);

      // DONE: 
      // 1. get the faulting address from stval register
      asm volatile("csrr %0, stval" : "=r"(stval));
      

      printf("Page fault! epc = 0x%016lx, stval = 0x%016lx\n", epc, stval);


      struct vm_area_struct* vma;
      list_for_each_entry(vma, &current->mm.vm->vm_list, vm_list) {
        // DONE: 
        // 2. check whether the faulting address is in the range of a vm area
        // 3. check the permission of the vm area. The vma must be PTE_X/R/W according to the faulting cause, and also be PTE_V, PTE_U
        // 4. if the faulting address is valid, allocate physical pages, map it to the vm area, mark the vma mapped(vma->mapped = 1), and return
        // 5. otherwise, print error message and add 4 to the sepc
        // 6. if the faulting address is not in the range of any vm area, add 4 to the sepc (DONE)
        if (vma->vm_start <= stval && stval <= vma->vm_end) {
          if (
              (vma->vm_flags & PTE_U) && 
              (vma->vm_flags & PTE_V) &&
              (
                (cause==0xc && (vma->vm_flags & PTE_X)) || 
                (cause==0xd && (vma->vm_flags & PTE_R)) || 
                (cause==0xf && (vma->vm_flags & PTE_W) && (vma->vm_flags & PTE_R))
              )
          ) {
            uint64_t* pgtbl = (current->satp & 0x00000fffffffffff) << 12;
            uint64_t pa = alloc_pages((vma->vm_end - vma->vm_start) / PAGE_SIZE);
            create_mapping(pgtbl, vma->vm_start, pa, vma->vm_end - vma->vm_start, vma->vm_flags);
            vma->mapped=1;
            return;
          }
          else {
            printf("Permission denied! Required: 0x%lx, Actual: 0x%lx\n", 
                (cause==0xc ? PTE_X : (cause==0xd ? PTE_R : PTE_W)),
                vma->vm_flags
            );
            sp_ptr[16] += 4;
            return;
          }
        }
      }
      printf("Unhandled page fault! addr = 0x%016lx\n", stval);
      sp_ptr[16] += 4;
      return;
    }
    // syscall from user mode
    else if (cause == 0x8) {
      // 根据我们规定的接口规范，从a7中读取系统调用号，然后从a0~a5读取参数，调用对应的系统调用处理函数，最后把返回值保存在a0~a1中。
      // 注意读取和修改的应该是保存在栈上的值，而不是寄存器中的值，因为寄存器上的值可能被更改。

      // 1. 从 a7 中读取系统调用号
      // 2. 从 a0 ~ a5 中读取系统调用参数
      // 2. 调用syscall()，并把返回值保存到 a0,a1 中
      // 3. sepc += 4，注意应该修改栈上的sepc，而不是sepc寄存器
      
      // 提示，可以用(uint64_t*)(sp)得到一个数组
      
      uint64_t* sp_ptr = (uint64_t*)(sp);
      uint64_t syscall_num = sp_ptr[11];
      uint64_t arg0 = sp_ptr[4], arg1 = sp_ptr[5], arg2 = sp_ptr[6], arg3 = sp_ptr[7], arg4 = sp_ptr[8], arg5 = sp_ptr[9];

      struct ret_info ret = syscall(syscall_num, arg0, arg1, arg2, arg3, arg4, arg5);
      sp_ptr[4] = ret.a0;
      sp_ptr[5] = ret.a1;
      sp_ptr[16] += 4;
      
    }
    else {
      printf("Unknown exception! epc = 0x%016lx\n", epc);
      while (1);
    }
  }
  return;
}
