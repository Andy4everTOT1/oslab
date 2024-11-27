#include "syscall.h"

#include "task_manager.h"
#include "stdio.h"
#include "defs.h"


struct ret_info syscall(uint64_t syscall_num, uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
    // DONE: implement syscall function
    struct ret_info ret;
    switch (syscall_num)
    {
    case SYS_WRITE: {
        // sys_write(unsigned int fd, const char* buf, size_t count)
        unsigned int fd = arg0;
        const char *buf = (const char *)arg1;
        size_t count = arg2;
        if (fd != 1)
        {
            printf("Unknown fd! fd = %d\n", arg0);
            while (1);
            break;
        }
        uint64_t i;
        for (i = 0; i < count; ++i)
            putchar(buf[i]);
        ret.a0 = i;
        break;
    }
    case SYS_GETPID:
        // sys_getpid()
        ret.a0 = getpid();
        break;

    default:
        printf("Unknown syscall! syscall_num = %d\n", syscall_num);
        while (1);
        break;
    }
    return ret;
}