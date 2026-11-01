#include "kfork.h"

#include "kernel/memory/kmemory.h"
#include "kernel/memory/kmemory_internal.h"
#include "kernel/memory/kalloc.h"


static char program_buffer[MI_CORE_PAGE_SIZE];
static char exec_path_buffer[64];
static ProcessControlBlock process_pcb[MAX_PROCESS_COUNT]; // 进程数组，索引为pid
static uint8_t process_used[MAX_PROCESS_COUNT];            // 该 pid 是否已被占用
static pid_t curr_process = -1;                            // 当前进程pid

static void miCoreProcessBootstrap(void);

// 在新进程内核栈顶构造一份 switch_to 能恢复的初始上下文:
// 第一次切到它时, ret 会跳到 miCoreProcessBootstrap
static void miCoreInitProcessContext(ProcessControlBlock* pcb) {
    uint32_t* frame =
        (uint32_t*)(pcb->pmpa.begin_of_core_stack - 10 * sizeof(uint32_t));

    frame[0] = 0;       // EDI
    frame[1] = 0;       // ESI
    frame[2] = 0;       // EBP
    frame[3] = 0;       // ESP(popad 会忽略)
    frame[4] = 0;       // EBX
    frame[5] = 0;       // EDX
    frame[6] = 0;       // ECX
    frame[7] = 0;       // EAX
    frame[8] = 0x2;     // EFLAGS, IF=0
    frame[9] = (uint32_t)miCoreProcessBootstrap;   // ret 目标

    pcb->kernel_esp = (addr_type)frame;
}


// 只分配 PCB/页目录/内核栈, 不初始化第一次运行的 bootstrap 上下文
static pid_t miCoreCreateProcessRaw(void) {
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

        // 每进程内核栈从内核堆拿一块, 16KB
        void* kernel_stack = miCoreKernelAllocPage(CORE_STACK_SIZE);

        if (kernel_stack == 0) {
            miCoreDestroyTopPageDirectory(pd_phys);
            return -1;
        }

        // 用户栈/堆的初始范围, 页表等真正访问时再按需建立
        pcb->pmpa.begin_of_user_stack = BEGIN_OF_USER_STACK;
        pcb->pmpa.end_of_user_stack = END_OF_USER_STACK;
        pcb->pmpa.begin_of_user_heap = BEGIN_OF_USER_HEAP;
        pcb->pmpa.end_of_user_heap =
            BEGIN_OF_USER_HEAP + PROCESS_DEFAULT_HEAP_SIZE;
        pcb->pmpa.end_of_core_stack = (addr_type)kernel_stack;
        pcb->pmpa.begin_of_core_stack =
            (addr_type)kernel_stack + CORE_STACK_SIZE;

        process_used[pid] = 1;
        return pid;
    }

    return -1;
}


// 创建一个空进程: 第一次切过去先跑 bootstrap, 加载用户程序
pid_t miCoreCreateProcess(void) {
    pid_t pid = miCoreCreateProcessRaw();

    if (pid >= 0) {
        miCoreInitProcessContext(&process_pcb[pid]);
    }

    return pid;
}


// 把父进程 1GB-4GB 用户区的页面复制到子进程
static int miCoreCopyUserAddressSpace(ProcessControlBlock* parent,
                                      ProcessControlBlock* child) {
    uint32_t* parent_pd = (uint32_t*)parent->page_directory;

    for (uint32_t pde = 256; pde < 1024; pde++) {
        if ((parent_pd[pde] & PAGE_PRESENT) == 0) {
            continue;
        }

        size_t slot = miCoreGetPageTableSlot(parent->page_directory, pde);

        if (slot == 0) {
            return -1;
        }

        uint32_t* parent_pt =
            (uint32_t*)(PAGE_TABLE_WINDOW_BEGIN + slot * PAGE_SIZE);

        for (uint32_t pte = 0; pte < 1024; pte++) {
            uint32_t pte_val = parent_pt[pte];

            if ((pte_val & PAGE_PRESENT) == 0) {
                continue;
            }

            uint32_t child_pa = miCoreMemoryAllocPage();

            if (child_pa == 0) {
                return -1;
            }

            addr_type tmp = miCoreMapPageTemp(child_pa);

            if (tmp == 0) {
                miCoreMemoryFreePage(child_pa);
                return -1;
            }

            uint32_t va = (pde << 22) | (pte << 12);

            memcpy((void*)tmp, (void*)va, PAGE_SIZE);
            miCoreUnmapPageTemp(tmp);

            miCorePageMap(
                child->page_directory,
                va,
                child_pa,
                pte_val & 0xFFF
            );
        }
    }

    return 0;
}


// fork: 复制当前进程地址空间, 构造子进程的 syscall 返回现场
pid_t miCoreFork(uint32_t* parent_frame) {
    ProcessControlBlock* parent = miCoreCurrentProcess();

    if (parent == 0 || parent_frame == 0) {
        return -1;
    }

    pid_t child_pid = miCoreCreateProcessRaw();

    if (child_pid < 0) {
        return -1;
    }

    ProcessControlBlock* child = &process_pcb[child_pid];

    if (miCoreCopyUserAddressSpace(parent, child) != 0) {
        miCoreCleanProcess(child_pid);
        return -1;
    }

    // 用户栈/堆范围和父进程一致
    child->pmpa.begin_of_user_stack = parent->pmpa.begin_of_user_stack;
    child->pmpa.end_of_user_stack = parent->pmpa.end_of_user_stack;
    child->pmpa.begin_of_user_heap = parent->pmpa.begin_of_user_heap;
    child->pmpa.end_of_user_heap = parent->pmpa.end_of_user_heap;

    // 子进程内核栈上放两份帧:
    //   switch frame -> ret 到 syscall_return
    //   syscall frame -> popad/iret 回用户态, EAX=0
    uint32_t* top = (uint32_t*)child->pmpa.begin_of_core_stack;
    uint32_t* sysframe = top - 17;
    uint32_t* switchframe = sysframe - 10;

    for (size_t i = 0; i < 17; i++) {
        sysframe[i] = parent_frame[i];
    }

    sysframe[7] = 0;        // 子进程 fork 返回 0

    for (size_t i = 0; i < 8; i++) {
        switchframe[i] = 0;
    }

    switchframe[8] = 0x2;                       // EFLAGS
    switchframe[9] = (uint32_t)syscall_return;  // ret 目标

    child->kernel_esp = (addr_type)switchframe;

    return child_pid;
}


// 切换到指定进程:
//   新进程的 kernel_esp 在创建时已经初始化, 第一次切过去会先跑 bootstrap;
//   旧进程的上下文由 mi_core_switch_process_to 保存到 PCB->kernel_esp。
int miCoreSwitchProcess(pid_t pid) {
    if (pid < 0 || pid >= MAX_PROCESS_COUNT || !process_used[pid]) {
        return -1;
    }

    if (pid == curr_process) {
        return 0;
    }

    ProcessControlBlock* next = &process_pcb[pid];

    if (next->kernel_esp == 0) {
        return -1;
    }

    pid_t old = curr_process;

    miCoreSetTSSEsp0(next->pmpa.begin_of_core_stack);
    curr_process = pid;

    if (old < 0) {
        // 还没有当前进程, 保存到一个临时位置即可
        static uint32_t boot_esp;
        mi_core_switch_process_to(
            &boot_esp,
            next->kernel_esp,
            next->phys_page_directory
        );
        return 0;
    }

    ProcessControlBlock* current = &process_pcb[old];

    mi_core_switch_process_to(
        &current->kernel_esp,
        next->kernel_esp,
        next->phys_page_directory
    );

    // 本进程以后被切回来时会从这里继续返回
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

    // 内核栈还给内核堆
    miCoreKernelFreePage((void*)pcb->pmpa.end_of_core_stack);

    memset(pcb, 0, sizeof(*pcb));
    process_used[pid] = 0;
    return 0;
}


// 当前进程 PCB, 缺页处理用它判断故障地址属于哪个用户段
ProcessControlBlock* miCoreCurrentProcess(void) {
    if (curr_process < 0 || curr_process >= MAX_PROCESS_COUNT ||
        !process_used[curr_process]) {
        return 0;
    }

    return &process_pcb[curr_process];
}


pid_t miCoreGetPid(void) {
    return curr_process;
}


// 把用户传进来的路径先拷到内核缓冲区, 再加载执行.
// 不能直接用用户指针: 加载新程序会覆盖 1GB 处的旧代码, 路径可能就在里面.
pid_t miCoreExec(const char* path) {
    if (path == 0) {
        return -1;
    }

    size_t len = 0;

    while (len < sizeof(exec_path_buffer) - 1 && path[len] != '\0') {
        exec_path_buffer[len] = path[len];
        len++;
    }

    if (path[len] != '\0') {
        return -1;
    }

    exec_path_buffer[len] = '\0';

    miCoreProgramLoader(exec_path_buffer);

    // 加载/进入失败才会走到这里
    return -1;
}


// 非法缺页: 不补页, 标记当前进程退出并停机
void miCoreHandleSegfault(uint32_t addr) {
    kVagprintf("segfault at 0x%x, pid=%d\n", addr, curr_process);

    if (curr_process >= 0 && curr_process < MAX_PROCESS_COUNT) {
        process_used[curr_process] = 0;
    }

    curr_process = -1;

    // 还没有调度器可以切到别的进程, 先停机
    for (;;) {
        asm volatile("cli; hlt");
    }
}


// 新进程第一次被切到时的内核入口:
// 此时已经在自己的 CR3 和内核栈上, 在这里加载用户程序并进入 ring3
static void miCoreProcessBootstrap(void) {
    miCoreProgramLoader("/test/hellominux");

    // 加载失败时不能让 CPU 跑飞
    for (;;) {
        asm volatile("cli; hlt");
    }
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
