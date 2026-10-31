#pragma once
#include <stdint.h>

struct Process {
    uint32_t pid;                       // 进程pid

    uint32_t cr3;                       // 页表指针

    uint32_t user_stack_top;            // 用户栈顶部（高地址
    uint32_t user_stack_bottom;         // 用户栈底（低地址

    uint32_t kernel_stack_top;
    uint32_t kernel_stack_bottom;

    uint32_t kernel_esp;                // esp寄存器值

    uint32_t state;                     // 进程状态 0（ 1（执行中 2（阻塞
};
