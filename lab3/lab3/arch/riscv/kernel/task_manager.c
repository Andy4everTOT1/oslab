#include "task_manager.h"
#include "vm.h"

struct task_struct *task[NR_TASKS];

int task_init_done = 0;


extern uint64_t text_start;
extern uint64_t rodata_start;
extern uint64_t data_start;
extern uint64_t _end;
extern uint64_t user_program_start;


// initialize tasks, set member variables
void task_init(void) {
  puts("task init...\n");

  for(int i = 0; i < LAB_TEST_NUM; ++i) {
    // DONE
    // initialize task[i]
    // get the task_struct using alloc_page()
    // set state = TASK_RUNNING, counter = 0, priority = 5, 
    // blocked = 0, pid = i, thread.sp, thread.ra
    struct task_struct* new_task =(struct task_struct*)(VIRTUAL_ADDR(alloc_page()));
    new_task->state = TASK_RUNNING;
    new_task->counter = 0;
    new_task->priority = 5;
    new_task->blocked = 0;
    new_task->pid = i;
    task[i] = new_task;
    task[i]->thread.sp = (uint64_t)task[i] + PAGE_SIZE; // 内核栈的栈底
    task[i]->thread.ra = (uint64_t)__init_sepc;

    
    // DONE: task[i]的物理地址
    uint64_t task_addr = PHYSICAL_ADDR((uint64_t)&user_program_start) + i * PAGE_SIZE;

    // TODO: 完成用户栈的分配，并创建页表项，将用户栈映射到实际的物理地址

    // 1. 为用户栈分配物理页面，使用alloc_page函数
    uint64_t* sp = alloc_page();
    // 2. 为用户进程分配根页表，使用alloc_page函数
    uint64_t* pgtbl = alloc_page();
    // 3. 将task[i]->sscratch指定为虚拟空间下的栈地址，即0x1001000 + PAGE_SIZE（注意栈是从高地址到低地址使用的）
    task[i]->sscratch = (uint64_t)0x1001000 + PAGE_SIZE;
    // 4. 正确设置task[i]->satp，注意设置ASID
    task[i]->satp = (8L << 60) | ((uint64_t)i << 44) | ((uint64_t)pgtbl >> 12);
    // 5. 将用户栈映射到实际的物理地址，使用create_mapping函数
    create_mapping(
      pgtbl,
      0x1001000,
      sp,
      4 << 10,
      PTE_V | PTE_R | PTE_W | PTE_U
    );
    // 6. 将用户程序映射到虚拟地址空间，使用create_mapping函数
    create_mapping(
      pgtbl,
      0x1000000,
      task_addr,
      4 << 10,
      PTE_V | PTE_R |PTE_X | PTE_W | PTE_U
    );
    // 7. 将将虚拟地址 0xffffffc000000000 开始的 16 MB 空间映射到起始物理地址为 0x80000000 的 16MB 地址空间，注意此时 &rodata_start、... 得到的是虚拟地址还是物理地址？我们需要的是什么地址？
    create_mapping(
      pgtbl,
      0xffffffc000000000,
      0x80000000,
      16 << 20,
      PTE_V | PTE_R | PTE_W | PTE_X
    );
    // 8. 对内核起始地址 0x80000000 的16MB空间做等值映射（将虚拟地址 0x80000000 开始的 16 MB 空间映射到起始物理地址为 0x80000000 的 16MB 空间），PTE_V | PTE_R | PTE_W | PTE_X 为映射的读写权限。
    create_mapping(
      pgtbl,
      0x80000000,
      0x80000000,
      16 << 20,
      PTE_V | PTE_R | PTE_W | PTE_X
    );
    // 9. 修改对内核空间不同 section 所在页属性的设置，完成对不同section的保护，其中text段的权限为 r-x, rodata 段为 r--, 其他段为 rw-，注意上述两个映射都需要做保护。
    // text段权限为r-x
    create_mapping(
      pgtbl,
      VIRTUAL_ADDR(&text_start),
      PHYSICAL_ADDR(&text_start),
      (&rodata_start - &text_start) * sizeof(uint64_t),
      PTE_V | PTE_R | PTE_X
    ); 
    // rodata段权限为r--
    create_mapping(
      pgtbl,
      VIRTUAL_ADDR(&rodata_start),
      PHYSICAL_ADDR(&rodata_start),
      (&data_start - &rodata_start) * sizeof(uint64_t),
      PTE_V | PTE_R
    );
    // 其他段权限为rw-
    create_mapping(
      pgtbl,
      VIRTUAL_ADDR(&data_start),
      PHYSICAL_ADDR(&data_start),
      (&_end - &data_start) * sizeof(uint64_t),
      PTE_V | PTE_R | PTE_W
    );
    // 10. 将必要的硬件地址（如 0x10000000 为起始地址的 UART ）进行等值映射 ( 可以映射连续 1MB 大小 )，无偏移，PTE_V | PTE_R | PTE_W 为映射的读写权限
    create_mapping(
      pgtbl,
      0x10000000,
      0x10000000,
      1 << 20,
      PTE_V | PTE_R | PTE_W
    );

    printf("[PID = %d] Process Create Successfully!\n", task[i]->pid);
  }
  task_init_done = 1;
}