#include "kfork.h"



static char program_buffer[MI_CORE_PAGE_SIZE];

// 程序加载：读取裸二进制文件，将其连续映射到用户空间 0x400000 开始的位置。
void miCoreProgramLoader(const char* path) {
    uint32_t cr3 = miCoreGetCR3();
    uint32_t offset = 0;

    // 程序代码按照 4KB 页面逐页加载。
    // 每次从文件读取一个页面大小的数据，然后为该页面分配物理页并建立用户映射。
    while (1) {
        int size = miCoreReadFile(
            (char*)path,
            program_buffer,
            offset,
            sizeof(program_buffer)
        );

        if (size < 0) {
            kVagprintf(
                "program read failed: %s, offset=%u\n",
                path,
                offset
            );
            return;
        }

        if (size == 0) {
            break;
        }

        uint32_t virtual_addr = USER_CODE_VADDR + offset;

        uint32_t physical_addr = miCoreMemoryAllocPage();

        if (physical_addr == 0) {
            kVagprintf(
                "program memory allocation failed: offset=%u\n",
                offset
            );
            return;
        }

        // 当前页面必须具有用户权限，否则 Ring3 取指时会触发页错误。
        miCorePageMap(
            cr3,
            virtual_addr,
            physical_addr,
            PAGE_PRESENT | PAGE_WRITE | PAGE_USER
        );

        // 页面已经建立映射，内核可以直接通过用户虚拟地址写入程序。
        memcpy(
            (void*)virtual_addr,
            program_buffer,
            size
        );

        offset += size;

        kVagprintf(
            "load page: va=0x%x, size=%u\n",
            virtual_addr,
            size
        );

        // 最后一页不足 4KB，说明已经到达文件末尾。
        if (size < MI_CORE_PAGE_SIZE) {
            break;
        }
    }

    if (offset == 0) {
        kVagprintf("program is empty: %s\n", path);
        return;
    }

    // 程序代码加载完成后，再单独建立用户栈。
    // 栈顶位于 USER_STACK_TOP，向低地址方向增长。
    uint32_t stack_page = miCoreMemoryAllocPage();

    if (stack_page == 0) {
        kVagprintf("cannot allocate user stack\n");
        return;
    }

    miCorePageMap(
        cr3,
        USER_STACK_VADDR,
        stack_page,
        PAGE_PRESENT | PAGE_WRITE | PAGE_USER
    );

    kVagprintf(
        "load user program: %s, size=%u, entry=0x%x, stack=0x%x\n",
        path,
        offset,
        USER_CODE_VADDR,
        USER_STACK_TOP
    );

    // 构造 Ring3 初始执行现场。
    // 裸二进制从文件第 0 字节开始执行，因此入口地址固定为 USER_CODE_VADDR。
    enter_user_mode(
        USER_CODE_VADDR,
        USER_STACK_TOP
    );
}



void exit(int status){
    kVagprintf("power off---------\n");
    asm volatile(
        "mov $0, %%eax\n"
        "int $0x80\n"
        :
        :
        : "eax"
    );
}
