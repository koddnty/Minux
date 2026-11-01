/* 内核 C 主体。
 * kernel_entry.asm 已经把 CS/DS/SS 拉平、栈切好、.bss 清零，然后 call 到这里。
 */
#include "syscall/kprintf.h"
#include <stdbool.h>
#include <stdint.h>
#include "memory/kmemory.h"
#include "proc/kfork.h"



int kernel_main(void) {
    kVagprintf("success: init gdt\n");
    kVagprintf("success: init tss\n");
    miCoreMemoryInit();     // 初始化物理内存位图
    kVagprintf("success: init physical memory\n");

    // 初始化页表专区, 并把 0-1GB 的二级页表全部建好(900MB-1GB 窗口 + 0-900MB 内核页表)
    if (miCorePageArenaInit() != 0) {
        kVagprintf("failed: init page table arena\n");
        while (1);
    }
    kVagprintf("success: init page table arena\n");

    // 先建一个进程并切过去, 之后的用户缺页才有当前进程的栈/堆范围可查
    pid_t pid = miCoreCreateProcess();
    if (pid < 0) {
        kVagprintf("failed: create process\n");
        while (1);
    }
    kVagprintf("start process: %d\n", pid);

    // 切过去后不会返回: 新进程的 bootstrap 会加载用户程序并进入 ring3
    if (miCoreSwitchProcess(pid) != 0) {
        kVagprintf("failed: switch process\n");
        while (1);
    }

    for (;;) {
        asm volatile("cli; hlt");
    }
}
