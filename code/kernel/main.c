/* 内核 C 主体。
 * kernel_entry.asm 已经把 CS/DS/SS 拉平、栈切好、.bss 清零，然后 call 到这里。
 */
#include "lib/kprintf.h"
#include <stdbool.h>
#include <stdint.h>
#include "lib/kmemory.h"

// 用户态测试用的地址: 都在映射表里显式建成 U/S=1, 和内核区彻底分开
#define USER_CODE_VADDR   0x00500000u
#define USER_STACK_VADDR  0x00502000u
#define USER_STACK_TOP    (USER_STACK_VADDR + PAGE_SIZE)
#define VIDEO_VADDR       0x000B8000u

extern void enter_user_mode(uint32_t eip, uint32_t esp);
extern char user_code_start[];      // kernel_entry.asm 里的用户代码片段(位置无关)
extern char user_code_end[];

int kernel_main(void) {
    uint32_t cr3 = miCoreGetCR3();
    uint32_t user_code_size = (uint32_t)(user_code_end - user_code_start);

    kVagprintf("success: init gdt\n");
    kVagprintf("success: init tss\n");
    miCoreMemoryInit();     // 初始化虚拟内存
    kVagprintf("success: init virtual memory\n");
    miCorePageArenaInit();  // 初始化页表专区(900MB 起)
    kVagprintf("success: init page table arena\n");

    // 自检: 给 4MB 处建映射, 会走"新建页表 + 专区"这条路
    uint32_t phys = miCoreMemoryAllocPage();
    miCorePageMap(cr3, 0x00400000, phys, PAGE_PRESENT | PAGE_WRITE);
    *(volatile uint32_t*)0x00400000 = 0x12345678;   // 写它, 踩到未映射页就会缺页

    // 专区不变式: 根表[槽位] == PDE 指向的物理页
    kVagprintf("arena: root[0]=0x%x PDE[1]=0x%x\n",
               *(volatile uint32_t*)PAGE_TABLE_ADDR,
               ((uint32_t*)cr3)[1]);
    kVagprintf("map test: 0x400000 -> 0x%x, read back 0x%x\n",
               phys, *(volatile uint32_t*)0x00400000);

    // 用户页: 代码页 + 栈页 + 显存页都要 U/S=1, 否则 ring3 一取指/一压栈就 #PF
    miCorePageMap(cr3, USER_CODE_VADDR,  miCoreMemoryAllocPage(), PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    miCorePageMap(cr3, USER_STACK_VADDR, miCoreMemoryAllocPage(), PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    miCorePageMap(cr3, VIDEO_VADDR,      VIDEO_VADDR,             PAGE_PRESENT | PAGE_WRITE | PAGE_USER);

    // 把用户代码搬到用户页(用户页已映射, 内核可以直接按虚拟地址写)
    memcpy((void*)USER_CODE_VADDR, user_code_start, user_code_size);

    kVagprintf("user: code %u bytes @0x%x, stack top 0x%x\n",
               user_code_size, USER_CODE_VADDR, USER_STACK_TOP);

    enter_user_mode(USER_CODE_VADDR, USER_STACK_TOP);   // 进 ring3, 不返回

    kVagprintf("finished test ring0->3->0\n");
    return 0;
}
