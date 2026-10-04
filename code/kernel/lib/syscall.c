#include "syscall.h"


// 打印
void write(const char* str) {
    asm volatile(
        "mov $1, %%eax\n"
        "mov %0, %%ebx\n"
        "int $0x80\n"
        :
        : "r"(str)
        : "eax", "ebx"
    );
}