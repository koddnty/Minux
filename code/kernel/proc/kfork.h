#pragma once
#include "kernel/syscall/kprintf.h"
#include "minFs/fsTree.h"
#include "kernel/memory/kmemory.h"
#include "kernel/base.h"
#define USER_CODE_VADDR  0x400000       // 用户代码段
#define USER_STACK_VADDR 0x800000       // 用户栈顶
#define USER_STACK_TOP   (USER_STACK_VADDR + MI_CORE_PAGE_SIZE)
/* repair: 用户程序唯一能看到输出的地方就是显存, 必须也建成 U/S=1,
 * repair: 否则 ring3 一写 0xB8000 就是保护性缺页(P=1), 直接被停机 */
#define USER_VIDEO_VADDR 0xB8000

#define PAGE_PRESENT 0x001
#define PAGE_WRITE   0x002
#define PAGE_USER    0x004


#define MAX_PROCESS_COUNT 128
#define PROCESS_DEFAULT_HEAP_SIZE  (4 * 1024 * 1024)        // 堆大小，

#define BEGIN_OF_CORE_HEAP // 你取合适值后告诉我
#define END_OF_CORE_HEAP // 你取合适值后告诉我



extern void enter_user_mode(uint32_t eip, uint32_t esp);

// 进程相关 --------------------------------------------------
typedef struct ProcessMemoryPartition {
    addr_type begin_of_user_heap;       // 用户堆起始
    addr_type end_of_user_heap;       // 用户堆尾 （初始化需要限制为PROCESS_HEAP_SIZE大小，后续可能不满足也行）
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




// 程序加载（编译时把入口放在0位置，加载到USER_CODE_VADDR（用户态开始)位置
void miCoreProgramLoader(const char* path);

// 程序退出
void exit(int status);