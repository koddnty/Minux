#pragma once
#include <stdint.h>
#include <string.h>
#include "kernel/base.h"

/**
 * koddnty
 *  内存管理，实现页表构建，物理内存-虚拟地址映射，维护物理内存
 */
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

// 内核堆默认范围: 0-1GB 内核区内, 低于 900MB 页表窗口
#define BEGIN_OF_CORE_HEAP      0x01000000
#define END_OF_CORE_HEAP        0x04000000

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

// 创建一张干净顶级页表，内核区域和当前页表一致,中途不允许中断(准备用作进程创建）,返回物理地址供cr3使用
phys_addr_type miCoreCreateTopPageDirectory();

// 查询顶级页表在 900MB-1GB 专区里的虚拟别名, 0 表示不是本内核创建的页目录
// 只映射一级页目录; 二级页表仍然在 miCorePageMap 里按需分配和映射
addr_type miCoreGetPageDirectoryVirt(phys_addr_type pd_phys);

