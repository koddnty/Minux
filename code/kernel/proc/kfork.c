#include "kfork.h"

#include "kernel/memory/kmemory.h"
#include "kernel/memory/kmemory_internal.h"


static char program_buffer[MI_CORE_PAGE_SIZE];
static ProcessControlBlock process_pcb[MAX_PROCESS_COUNT]; // 进程数组，索引为pid
static uint8_t process_used[MAX_PROCESS_COUNT];            // 该 pid 是否已被占用
static pid_t curr_process = -1;                            // 当前进程pid


// 创建一个空进程: 分配 PCB 和一张只带内核映射的页目录
pid_t miCoreCreateProcess() {
    for (pid_t pid = 0; pid < MAX_PROCESS_COUNT; pid++) {
        if (process_used[pid]) {
            continue;
        }

        phys_addr_type pd_phys = miCoreCreateTopPageDirectory();

        if (pd_phys == 0) {
            return -1;
        }

        addr_type pd_virt = miCoreGetPageDirectoryVirt(pd_phys);

        if (pd_virt == 0) {
            miCoreDestroyTopPageDirectory(pd_phys);
            return -1;
        }

        ProcessControlBlock* pcb = &process_pcb[pid];

        memset(pcb, 0, sizeof(*pcb));
        pcb->pid = pid;
        pcb->phys_page_directory = pd_phys;
        pcb->page_directory = pd_virt;

        process_used[pid] = 1;
        return pid;
    }

    return -1;
}


// 切换到指定进程: 只切 CR3, 不处理调度和 TSS
int miCoreSwitchProcess(pid_t pid) {
    if (pid < 0 || pid >= MAX_PROCESS_COUNT || !process_used[pid]) {
        return -1;
    }

    if (pid == curr_process) {
        return 0;
    }

    miCoreLoadCR3(process_pcb[pid].phys_page_directory);
    curr_process = pid;
    return 0;
}


// 清理进程: 释放用户数据页、用户页表、页表和页目录槽位
int miCoreCleanProcess(pid_t pid) {
    if (pid < 0 || pid >= MAX_PROCESS_COUNT || !process_used[pid]) {
        return -1;
    }

    // 当前正在用的页目录不能直接释放, 先切走
    if (pid == curr_process) {
        return -1;
    }

    ProcessControlBlock* pcb = &process_pcb[pid];
    addr_type pd_virt = pcb->page_directory;

    if (pd_virt == 0 || pcb->phys_page_directory == miCoreGetCR3()) {
        return -1;
    }

    // 先检查每个用户 PDE 都有可访问的页表槽位, 避免清到一半失败
    for (uint32_t pde = 256; pde < 1024; pde++) {
        uint32_t pde_val = ((uint32_t*)pd_virt)[pde];

        if ((pde_val & PAGE_PRESENT) &&
            miCoreGetPageTableSlot(pd_virt, pde) == 0) {
            return -1;
        }
    }

    // 释放用户页表里的数据页, 再释放页表本身
    for (uint32_t pde = 256; pde < 1024; pde++) {
        uint32_t pde_val = ((uint32_t*)pd_virt)[pde];

        if ((pde_val & PAGE_PRESENT) == 0) {
            continue;
        }

        uint32_t pt_phys = pde_val & 0xFFFFF000;
        size_t slot = miCoreGetPageTableSlot(pd_virt, pde);
        uint32_t* page_table =
            (uint32_t*)(PAGE_TABLE_WINDOW_BEGIN + slot * PAGE_SIZE);

        for (size_t i = 0; i < 1024; i++) {
            uint32_t pte = page_table[i];

            if (pte & PAGE_PRESENT) {
                miCoreMemoryFreePage(pte & 0xFFFFF000);
                page_table[i] = 0;
            }
        }

        miCoreReleasePageTableSlot(pd_virt, pde);
        miCoreMemoryFreePage(pt_phys);
        ((uint32_t*)pd_virt)[pde] = 0;
    }

    if (miCoreDestroyTopPageDirectory(pcb->phys_page_directory) != 0) {
        return -1;
    }

    memset(pcb, 0, sizeof(*pcb));
    process_used[pid] = 0;
    return 0;
}


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
