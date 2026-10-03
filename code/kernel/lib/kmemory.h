#pragma once
#include <stdint.h>
#include <string.h>

#define  MI_CORE_MEMORY_TOTAL_SIZE 32768
#define  MI_CORE_INIT_MEMORY_SIZE 1024
#define MI_CORE_PAGE_SIZE 4096


#define PAGE_DIRECTORY_ADDR 0x20000

#define PAGE_TABLE_ADDR     0x22000     // 900MB开头的页表(专区根表)
#define PAGE_TABLE_LOW_ADDR 0x21000     // 0~4MB恒等映射那张表(loader建的,内容不动,专区的槽位0就是它)
#define PAGE_TABLE_WINDOW_BEGIN 0x38400000
#define PAGE_TABLE_WINDOW_END   0x40000000
#define PAGE_SIZE               0x1000
#define PAGE_TABLE_VIRTUAL_SIZE 0x400000

// 页表项属性
#define PAGE_PRESENT 0x001
#define PAGE_WRITE   0x002
#define PAGE_USER    0x004

extern uint32_t miCoreGetCR2(void);
extern uint32_t miCoreGetCR3(void);
extern void miCoreLoadCR3(uint32_t cr3);
extern void miCoreInvalidatePage(uint32_t addr);

// 汇编api    --------------------------------------------------
// 中断处理入口
void miCorePageFaultHandler(uint32_t fault_addr);



// C kernel 系统调用
// 虚拟-物理地址映射建立
void miCorePageMap(uint32_t directory, uint32_t virtual, uint32_t physical, uint32_t flags);

// 虚拟-物理地址映射删除
void miCorePageMapErase(uint32_t directory, uint32_t virtual);

// 初始化页表专区: 所有页表都经 PAGE_TABLE_WINDOW_BEGIN 起的虚拟区读写, 不把物理地址当指针
int miCorePageArenaInit();

// 初始化汇编已占用内存
int miCoreMemoryInit();

// 分配物理内存页, 0失败， 其他为地址
uint32_t miCoreMemoryAllocPage();

// 释放物理内存页, 0成功， -1失败
int miCoreMemoryFreePage(uint32_t addr);
