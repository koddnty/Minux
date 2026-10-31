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
    miCoreMemoryInit();     // 初始化虚拟内存
    kVagprintf("success: init virtual memory\n");
    miCorePageArenaInit();  // 初始化页表专区(900MB 起)
    kVagprintf("success: init page table arena\n");


    // test
    miCoreProgramLoader("/test/hellominux");
    exit(0);
    return 0;
}
