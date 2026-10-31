#pragma once
#include "syscall/kprintf.h"
#include "minFs/fsTree.h"
#include "memory/kmemory.h"

#define USER_CODE_VADDR  0x400000       // 用户代码段
#define USER_STACK_VADDR 0x800000       // 用户栈顶
#define USER_STACK_TOP   (USER_STACK_VADDR + MI_CORE_PAGE_SIZE)
/* repair: 用户程序唯一能看到输出的地方就是显存, 必须也建成 U/S=1,
 * repair: 否则 ring3 一写 0xB8000 就是保护性缺页(P=1), 直接被停机 */
#define USER_VIDEO_VADDR 0xB8000

#define PAGE_PRESENT 0x001
#define PAGE_WRITE   0x002
#define PAGE_USER    0x004


extern void enter_user_mode(uint32_t eip, uint32_t esp);

// 程序加载（编译时把入口放在0位置，加载到USER_CODE_VADDR（用户态开始)位置
void miCoreProgramLoader(const char* path);


// 程序退出
void exit(int status);