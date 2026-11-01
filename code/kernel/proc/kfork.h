#pragma once
#include "kernel/syscall/kprintf.h"
#include "minFs/fsTree.h"
#include "kernel/memory/kmemory.h"
#include "kernel/base.h"
#define USER_VIDEO_VADDR 0xB8000

#define MAX_PROCESS_COUNT 128
#define PROCESS_DEFAULT_HEAP_SIZE  (4 * 1024 * 1024)        // 用户堆初始大小


// 内核内存段默认值(都在 0-1GB 内核区内, 使用时再建立映射)
#define CORE_STACK_SIZE     0x4000           // 每进程内核栈 16KB, 从内核堆分配

// 用户内存段默认设置(都在 1-3GB 用户区内)
#define USER_CODE_VADDR     0x40000000       // 1GB, 用户代码段
#define BEGIN_OF_USER_STACK 0x80000000       // 2GB, 用户栈底
#define END_OF_USER_STACK   0x80008000       // 2GB + 64KB, 用户栈顶
#define BEGIN_OF_USER_HEAP  0xB0000000       // 2.75GB, 用户堆起始
#define END_OF_USER_HEAP    0xC0000000       // 3GB, 用户堆最大结束

// 栈从高地址往低地址长, 初始只映射栈顶一页
#define USER_STACK_TOP      END_OF_USER_STACK
#define USER_STACK_VADDR    (USER_STACK_TOP - MI_CORE_PAGE_SIZE)



extern void enter_user_mode(uint32_t eip, uint32_t esp);
extern void miCoreSetTSSEsp0(uint32_t esp0);

// 进程相关 --------------------------------------------------
typedef struct ProcessMemoryPartition {
    addr_type begin_of_core_stack;      // 进程内核栈顶
    addr_type end_of_core_stack;        // 进程内核栈底
    addr_type begin_of_user_stack;      // 用户栈底
    addr_type end_of_user_stack;        // 用户栈顶
    addr_type begin_of_user_heap;       // 用户堆起始
    addr_type end_of_user_heap;         // 用户堆当前结束, 初始为起始 + PROCESS_DEFAULT_HEAP_SIZE
} ProcessMemoryPartition;

typedef struct ProcessControlBlock {
    pid_t pid;          // 进程pid
    ProcessMemoryPartition pmpa;      // 进程内存分区
    phys_addr_type phys_page_directory;           // 顶级页表指针（物理地址）
    addr_type page_directory;           // 顶级页表指针（虚拟地址）
} ProcessControlBlock;


// 进程创建, 返回进程pid(与索引对应)
pid_t miCoreCreateProcess();
// 切换进程到 pid进程，不存在则返回-1
int miCoreSwitchProcess(pid_t pid);
// 清理进程数据（页表等）
int miCoreCleanProcess(pid_t pid);

// 当前进程 PCB, 没有当前进程返回 0
ProcessControlBlock* miCoreCurrentProcess(void);

// 非法缺页: 打印 segfault, 标记当前进程退出并停机
void miCoreHandleSegfault(uint32_t addr);




// 程序加载（编译时把入口放在0位置，加载到USER_CODE_VADDR（用户态开始)位置
void miCoreProgramLoader(const char* path);

// 程序退出
void exit(int status);