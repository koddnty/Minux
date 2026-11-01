//
// Created by koddnty on 2026/11/1.
//

#include "threads.h"

// 用户侧 fork 包装: 系统调用号 2, 返回值放在 EAX
pid_t fork(void) {
    pid_t ret;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(2)
        : "memory"
    );

    return ret;
}

// 执行 path 指向的程序, 系统调用号 3
pid_t exec(const char* pwd) {
    pid_t ret;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(3), "b"(pwd)
        : "memory"
    );

    return ret;
}

// 获取当前进程 pid, 系统调用号 4
pid_t get_pid(void) {
    pid_t ret;

    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(4)
        : "memory"
    );

    return ret;
}
