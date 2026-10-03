/* 内核 C 主体。
 * kernel_entry.asm 已经把 CS/DS/SS 拉平、栈切好、.bss 清零，然后 call 到这里。
 */
#include "lib/kprintf.h"
#include <stdbool.h>
#include <stdint.h>
#include "lib/kmemory.h"


int kernel_main(void) {
    kVagprintf("success: init gdt\n");
    kVagprintf("success: init tss\n");
    miCoreMemoryInit();     // 初始化虚拟内存
    kVagprintf("success: init virtual memory\n");
    miCorePageArenaInit();  // 初始化页表专区(900MB 起)
    kVagprintf("success: init page table arena\n");

    // 自检: 给 4MB 处建映射, 会走"新建页表 + 专区"这条路
    uint32_t phys = miCoreMemoryAllocPage();
    miCorePageMap(miCoreGetCR3(), 0x00400000, phys, PAGE_PRESENT | PAGE_WRITE);
    *(volatile uint32_t*)0x00400000 = 0x12345678;   // 写它, 踩到未映射页就会缺页

    // 专区不变式: 根表[槽位] == PDE 指向的物理页
    uint32_t root_slot1 = *(volatile uint32_t*)(PAGE_TABLE_ADDR + 4);
    uint32_t pde1 = ((uint32_t*)miCoreGetCR3())[1];

    kVagprintf("arena: root[0]=0x%x root[1]=0x%x PDE[1]=0x%x\n",
               *(volatile uint32_t*)PAGE_TABLE_ADDR, root_slot1, pde1);
    kVagprintf("map test: 0x400000 -> 0x%x, read back 0x%x\n",
               phys, *(volatile uint32_t*)0x00400000);

    return 0;
}
