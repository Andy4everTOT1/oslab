#include "sched.h"
#include "test.h"
#include "stdio.h"

#define Kernel_Page 0x80210000
#define LOW_MEMORY 0x80211000
#define PAGE_SIZE 4096UL

extern void __init_sepc();

struct task_struct* current;
struct task_struct* task[NR_TASKS];

// If next==current,do nothing; else update current and call __switch_to.
void switch_to(struct task_struct* next) {
  if (current != next) {
    struct task_struct* prev = current;
    current = next;
    __switch_to(prev, next);
  }
}

int task_init_done = 0;
// initialize tasks, set member variables
void task_init(void) {
    puts("task init...\n");

    for (int i = 0; i < LAB_TEST_NUM; ++i) {
        // 获取 task_struct 的指针，基于 Kernel_Page 地址和任务索引 i
        task[i] = (struct task_struct*)(Kernel_Page + i * TASK_SIZE);
        
        // 初始化任务的各个字段
        task[i]->state = TASK_RUNNING;
        task[i]->counter = LAB_TEST_COUNTER; // 初始化时间片
        task[i]->priority = 5; // 设置默认优先级为 5
        task[i]->blocked = 0;
        task[i]->pid = i;
        
        // 初始化线程结构中的栈指针和返回地址
        task[i]->thread.sp = LOW_MEMORY - i * PAGE_SIZE;
        task[i]->thread.ra = (unsigned long long)__init_sepc;

        printf("[PID = %d] Process Create Successfully!\n", task[i]->pid);
    }
    
    task_init_done = 1;
}

void call_first_process() {
  // set current to 0x0 and call schedule()
  current = (struct task_struct*)(Kernel_Page + LAB_TEST_NUM * PAGE_SIZE);
  current->pid = -1;
  current->counter = 0;
  current->priority = 0;

  schedule();
}


void show_schedule(unsigned char next) {
  // show the information of all task and mark out the next task to run
  for (int i = 0; i < LAB_TEST_NUM; ++i) {
    if (task[i]->pid == next) {
      printf("task[%d]: counter = %d, priority = %d <-- next\n", i,
             task[i]->counter, task[i]->priority);
    } else {
      printf("task[%d]: counter = %d, priority = %d\n", i, task[i]->counter,
           task[i]->priority);
    }
  }
}

#ifdef SJF
// simulate the cpu timeslice, which measn a short time frame that gets assigned
// to process for CPU execution
void do_timer(void) {
    if (!task_init_done) return;

    printf("[*PID = %d] Context Calculation: counter = %d, priority = %d\n",
           current->pid, current->counter, current->priority);

    // 当前进程的剩余时间减 1
    current->counter--;
    if (current->counter == 0) {
        // 运行时间已耗尽，调用调度函数
        schedule();
    }
}


// Select the next task to run. If all tasks are done(counter=0), reinitialize all tasks.
void schedule(void) {
    struct task_struct* next_task = ((void *)0);
    int min_priority = __INT_MAX__;
    int min_counter = __INT_MAX__;
    unsigned char next = 0;

    // 遍历所有任务，从 LAST_TASK 到 FIRST_TASK
    for (int i = NR_TASKS - 1; i >= 0; --i) {
        if (task[i] && task[i]->state == TASK_RUNNING && task[i]->counter > 0) {
            // 选择剩余时间最少的任务，如果剩余时间相同，按遍历顺序选择
            if (task[i]->counter < min_counter) {
                min_priority = task[i]->priority;
                min_counter = task[i]->counter;
                next_task = task[i];
                next = i;
            }
        }
    }

    // 如果所有任务的剩余时间都为 0，重新初始化任务
    if (!next_task) {
        init_test_case();
        schedule();
        return;
    }

    // 切换到下一个任务
    show_schedule(next);
    switch_to(task[next]);
}


#endif

#ifdef PRIORITY

// simulate the cpu timeslice, which measn a short time frame that gets assigned
// to process for CPU execution
void do_timer(void) {
  if (!task_init_done) return;
  
  printf("[*PID = %d] Context Calculation: counter = %d, priority = %d\n",
         current->pid, current->counter, current->priority);
  
  // Reduce the remaining running time of the current process
  current->counter--;

  // Trigger the scheduler to enforce priority-based preemption
  schedule();
}

// Select the task with highest priority and lowest counter to run. If all tasks are done(counter=0), reinitialize all tasks.
void schedule(void) {
  struct task_struct* next_task = ((void *)0);
  int highest_priority = __INT_MAX__;
  int min_counter = __INT_MAX__;
  unsigned char next = 0;

  // Iterate through all tasks to find the one with the highest priority
  for (int i = NR_TASKS - 1; i >= 0; --i) {
      if (task[i] && task[i]->state == TASK_RUNNING && task[i]->counter > 0) {
          // Select task based on highest priority first
          if ((task[i]->priority < highest_priority) ||
              (task[i]->priority == highest_priority && task[i]->counter < min_counter)) {
              highest_priority = task[i]->priority;
              min_counter = task[i]->counter;
              next_task = task[i];
              next = i;
          }
      }
  }

  // If no runnable task is found, reinitialize tasks using init_test_case and reattempt scheduling
  if (!next_task) {
      init_test_case();
      schedule();
      return;
  }

  // Switch to the selected task
  show_schedule(next);
  switch_to(task[next]);
}

#endif