#include "defs.h"
#include "print.h"

extern struct sbiret sbi_call(uint64_t ext, uint64_t fid, uint64_t arg0,
                              uint64_t arg1, uint64_t arg2, uint64_t arg3,
                              uint64_t arg4, uint64_t arg5);

int puts(char* str){
	int count = 0;
    while (*str) {
        sbi_call(1, 0, *str, 0, 0, 0, 0, 0);
        str++;
        count++; 
    }
    return 0;
}

int put_num(uint64_t n){
    char buffer[21];
    int i = 0;
    if (n == 0) {
        sbi_call(1, 0, '0', 0, 0, 0, 0, 0);
        return 1;
    }
    while (n > 0) {
        buffer[i++] = '0' + (n % 10);  
        n /= 10; 
    }
    for (int j = i - 1; j >= 0; j--) {
        sbi_call(1, 0, buffer[j], 0, 0, 0, 0, 0);
    }
	return 0;
}

