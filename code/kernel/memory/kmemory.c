#include "kmemory.h"

#include "kernel/proc/kfork.h"




// 内存映射map
static uint32_t mi_core_memory_bit_map [MI_CORE_MEMORY_TOTAL_SIZE]; // 2 * 5 * 3 * 2K = 2^12 K = 4G 单位4K
static size_t mi_core_memory_min_begin_alloc = 0;           // 最小位可用内存

// 页表专区: 一张页表 = 专区里的一个槽位, 槽位 0 是 0~4MB 恒等映射那张表(0x21000)
#define MI_CORE_ARENA_BLOCK_COUNT 31
#define MI_CORE_ARENA_SLOT_COUNT (MI_CORE_ARENA_BLOCK_COUNT * 1024)
#define MI_CORE_MAX_PAGE_DIRECTORY 128
#define MI_CORE_PAGE_DIRECTORY_ROWS (MI_CORE_MAX_PAGE_DIRECTORY + 1)

static uint32_t mi_core_arena_slot_used[(MI_CORE_ARENA_SLOT_COUNT + 31) / 32];
static size_t mi_core_arena_slot_hint = 1;

// 每个页目录一份 PDE -> 专区槽位, 行 0 是启动时的页目录
static uint16_t mi_core_table_slot[MI_CORE_PAGE_DIRECTORY_ROWS][1024];

// 顶级页表(页目录)的物理地址和它在专区里的虚拟别名
static phys_addr_type mi_core_pd_phys[MI_CORE_MAX_PAGE_DIRECTORY];
static addr_type mi_core_pd_virt[MI_CORE_MAX_PAGE_DIRECTORY];
static size_t mi_core_pd_count = 0;

// 根据页目录物理地址或虚拟别名找到它对应的槽位表行
static int miCorePageDirectoryRow(uint32_t directory, uint32_t** page_directory) {
    if (directory == PAGE_DIRECTORY_ADDR) {
        *page_directory = (uint32_t*)PAGE_DIRECTORY_ADDR;
        return 0;
    }

    for (size_t i = 0; i < mi_core_pd_count; i++) {
        if (mi_core_pd_virt[i] == directory) {
            *page_directory = (uint32_t*)mi_core_pd_virt[i];
            return (int)i + 1;
        }
    }

    for (size_t i = 0; i < mi_core_pd_count; i++) {
        if (mi_core_pd_phys[i] == directory) {
            *page_directory = (uint32_t*)mi_core_pd_virt[i];
            return (int)i + 1;
        }
    }

    return -1;
}

static int miArenaSlotUsed(size_t slot) {
    return (mi_core_arena_slot_used[slot / 32] >> (slot % 32)) & 1u;
}

static void miArenaSlotMark(size_t slot, int used) {
    uint32_t mask = 1u << (slot % 32);

    if (used) {
        mi_core_arena_slot_used[slot / 32] |= mask;
    } else {
        mi_core_arena_slot_used[slot / 32] &= ~mask;
    }
}

// 槽位 0 和每个 4MB 块的最后一个槽位都不能当普通页表槽位分配
static void miArenaSlotInit(void) {
    memset(mi_core_arena_slot_used, 0, sizeof(mi_core_arena_slot_used));

    miArenaSlotMark(0, 1);

    for (size_t slot = 1023; slot < MI_CORE_ARENA_SLOT_COUNT; slot += 1024) {
        miArenaSlotMark(slot, 1);
    }

    mi_core_arena_slot_hint = 1;
}

// 专区的第 block 块(4MB)由哪张表管: 第 0 块是根表, 第 b(b>=1) 块放上一块最后一个槽位
static uint32_t* miArenaBlockTable(size_t block) {
    if (block == 0) {
        return (uint32_t*)PAGE_TABLE_ADDR;                  // 根表在低内存, 恒等可见
    }
    return (uint32_t*)(PAGE_TABLE_WINDOW_BEGIN + (block * 1024 - 1) * PAGE_SIZE);
}

// 按需把专区扩出第 block 块, 用多少映射多少
static void miArenaEnsureBlock(size_t block) {
    uint32_t* page_directory;
    int row = miCorePageDirectoryRow(miCoreGetCR3(), &page_directory);

    if (row < 0) {
        return;
    }

    size_t pde = (PAGE_TABLE_WINDOW_BEGIN >> 22) + block;

    if (block == 0 || (page_directory[pde] & PAGE_PRESENT)) {
        return;                                             // 第 0 块开机就建好了
    }

    miArenaEnsureBlock(block - 1);                          // 上一块先建好, 它映射着放表的位置

    uint32_t table = miCoreMemoryAllocPage();               // 这一块自己的表, 只能经专区访问
    if (table == 0) {
        return;
    }

    size_t slot = block * 1024 - 1;                         // 塞进上一块最后一个槽位
    miArenaBlockTable(block - 1)[slot % 1024] = table | PAGE_PRESENT | PAGE_WRITE;
    miCoreInvalidatePage(PAGE_TABLE_WINDOW_BEGIN + slot * PAGE_SIZE);

    memset((void*)miArenaBlockTable(block), 0, PAGE_SIZE);  // 新表经专区清零
    page_directory[pde] = table | PAGE_PRESENT | PAGE_WRITE;
    mi_core_table_slot[row][pde] = (uint16_t)slot;          // 长出来的块也要记下槽位
    miCoreInvalidatePage(PAGE_TABLE_WINDOW_BEGIN + block * PAGE_TABLE_VIRTUAL_SIZE);
}

// 取一个空闲槽位: 0 失败, 其他为槽位号
static size_t miArenaAllocSlot(void) {
    // 先从上次位置往后找, 找不到再从头找一次
    for (size_t pass = 0; pass < 2; pass++) {
        size_t begin = pass == 0 ? mi_core_arena_slot_hint : 1;
        size_t end = pass == 0 ? MI_CORE_ARENA_SLOT_COUNT : mi_core_arena_slot_hint;

        for (size_t slot = begin; slot < end; slot++) {
            if (miArenaSlotUsed(slot)) {
                continue;
            }

            miArenaSlotMark(slot, 1);
            mi_core_arena_slot_hint =
                (slot + 1 >= MI_CORE_ARENA_SLOT_COUNT) ? 1 : slot + 1;
            return slot;
        }
    }

    return 0;
}

// 归还一个槽位
static void miArenaFreeSlot(size_t slot) {
    if (slot == 0 || slot >= MI_CORE_ARENA_SLOT_COUNT) {
        return;
    }

    // 每个 4MB 块最后一个槽位留给下一块的表, 不参与普通分配
    if ((slot + 1) % 1024 == 0) {
        return;
    }

    miArenaSlotMark(slot, 0);

    if (slot < mi_core_arena_slot_hint) {
        mi_core_arena_slot_hint = slot;
    }
}

// 把槽位映射到物理页(写的是专区表, 永远可写)
static void miArenaSetSlot(size_t slot, uint32_t physical) {
    miArenaEnsureBlock(slot / 1024);
    miArenaBlockTable(slot / 1024)[slot % 1024] =
        (physical & 0xFFFFF000) | PAGE_PRESENT | PAGE_WRITE;
    miCoreInvalidatePage(PAGE_TABLE_WINDOW_BEGIN + slot * PAGE_SIZE);
}

// 清掉槽位里的映射, 但槽位本身由 miArenaFreeSlot 归还
static void miArenaClearSlot(size_t slot) {
    if (slot >= MI_CORE_ARENA_SLOT_COUNT) {
        return;
    }

    miArenaBlockTable(slot / 1024)[slot % 1024] = 0;
    miCoreInvalidatePage(PAGE_TABLE_WINDOW_BEGIN + slot * PAGE_SIZE);
}

// 临时映射一个物理页, 返回它在页表专区里的虚拟地址
addr_type miCoreMapPageTemp(phys_addr_type pa) {
    if (pa == 0) {
        return 0;
    }

    size_t slot = miArenaAllocSlot();

    if (slot == 0) {
        return 0;
    }

    miArenaSetSlot(slot, pa);
    return PAGE_TABLE_WINDOW_BEGIN + slot * PAGE_SIZE;
}

// 解除临时映射并归还槽位
void miCoreUnmapPageTemp(addr_type va) {
    if (va < PAGE_TABLE_WINDOW_BEGIN || va >= PAGE_TABLE_WINDOW_END) {
        return;
    }

    size_t slot = (va - PAGE_TABLE_WINDOW_BEGIN) / PAGE_SIZE;

    miArenaClearSlot(slot);
    miArenaFreeSlot(slot);
}

// 启动后把 0-1GB 的二级页表全部建好, 并填满一级页表(页目录)的 0-255 项。
// 这样以后内核堆/栈再建映射时, 所有进程页目录看到的都是同一组内核页表。
static int miArenaPopulateKernel(uint32_t* page_directory) {
    // PDE 0 用 loader 建好的 0-4MB 恒等映射表, 槽位 0 就是它
    mi_core_table_slot[0][0] = 0;

    // PDE 1-224: 普通二级页表, 先建空表
    for (uint32_t pde = 1; pde < 225; pde++) {
        size_t slot = miArenaAllocSlot();
        uint32_t table = miCoreMemoryAllocPage();

        if (slot == 0 || table == 0) {
            return -1;
        }

        miArenaSetSlot(slot, table);
        memset((void*)(PAGE_TABLE_WINDOW_BEGIN + slot * PAGE_SIZE), 0, PAGE_SIZE);

        page_directory[pde] = table | PAGE_PRESENT | PAGE_WRITE;
        mi_core_table_slot[0][pde] = (uint16_t)slot;
    }

    // PDE 225-255: 页表专区窗口, 把 30 个 block 表也提前长出来
    for (size_t block = 1; block < MI_CORE_ARENA_BLOCK_COUNT; block++) {
        miArenaEnsureBlock(block);

        size_t pde = (PAGE_TABLE_WINDOW_BEGIN >> 22) + block;

        if ((page_directory[pde] & PAGE_PRESENT) == 0) {
            return -1;
        }
    }

    return 0;
}

// 初始化页表专区: 槽位 0 指向 0~4MB 那张表(内容不动), 根表挂到 PDE
int miCorePageArenaInit() {
    uint32_t* root = (uint32_t*)PAGE_TABLE_ADDR;             // 低内存, 可直接写
    uint32_t* page_directory = (uint32_t*)miCoreGetCR3();

    miArenaSlotInit();

    memset(root, 0, PAGE_SIZE);
    root[0] = PAGE_TABLE_LOW_ADDR | PAGE_PRESENT | PAGE_WRITE;
    page_directory[PAGE_TABLE_WINDOW_BEGIN >> 22] = PAGE_TABLE_ADDR | PAGE_PRESENT | PAGE_WRITE;
    miCoreInvalidatePage(PAGE_TABLE_WINDOW_BEGIN);

    return miArenaPopulateKernel(page_directory);
}

void miCorePageFaultHandler(uint32_t fault_addr) {
    uint32_t virtual_addr =
        fault_addr & ~(MI_CORE_PAGE_SIZE - 1);

    ProcessControlBlock* pcb = miCoreCurrentProcess();
    int valid = 0;

    if (pcb != 0) {
        // 用户栈: 从高往低长, 栈底到栈顶之间都算合法
        if (virtual_addr >= pcb->pmpa.begin_of_user_stack &&
            virtual_addr < pcb->pmpa.end_of_user_stack) {
            valid = 1;
        }

        // 用户堆: 当前堆范围内按需补页
        if (virtual_addr >= pcb->pmpa.begin_of_user_heap &&
            virtual_addr < pcb->pmpa.end_of_user_heap) {
            valid = 1;
        }
    }

    if (!valid) {
        // 不在当前进程的用户栈/堆里, 不补页, 直接按 segfault 处理
        miCoreHandleSegfault(fault_addr);
        return;
    }

    uint32_t physical_addr =
        miCoreMemoryAllocPage();

    if (physical_addr == 0) {
        while (1);
    }

    miCorePageMap(
        miCoreGetCR3(),
        virtual_addr,
        physical_addr,
        PAGE_PRESENT | PAGE_WRITE | PAGE_USER
    );
}

// 建立虚拟地址到物理地址的映射
void miCorePageMap(const uint32_t directory, uint32_t const virtual, uint32_t const physical, uint32_t const flags) {
    // 把物理页目录/虚拟别名统一解析成可访问的页目录指针和槽位表行
    uint32_t* page_directory;
    int row = miCorePageDirectoryRow(directory, &page_directory);

    if (row < 0) {
        return;
    }

    uint32_t directory_index = virtual >> 22;
    uint32_t table_index = (virtual >> 12) & 0x3FF;

    // 专区自己那段地址由专区管, 不接受普通映射(否则会写坏页表)
    if (virtual >= PAGE_TABLE_WINDOW_BEGIN && virtual < PAGE_TABLE_WINDOW_END) {
        return;
    }

    // 页表一律通过专区虚拟地址访问, 绝不拿物理地址当指针
    uint32_t* page_table;

    // 如果对应的 PageTable 不存在，则分配一个物理页作为 PageTable
    if ((page_directory[directory_index] & PAGE_PRESENT) == 0) {
        size_t slot = miArenaAllocSlot();                   // 专区里取一个槽位放它

        if (slot == 0) {
            return;
        }

        uint32_t table = miCoreMemoryAllocPage();           // 页表的物理页(不能直接访问)

        if (table == 0) {
            miArenaFreeSlot(slot);
            return;
        }

        miArenaSetSlot(slot, table);                        // 槽位 -> 物理页
        page_table = (uint32_t*)(PAGE_TABLE_WINDOW_BEGIN + slot * PAGE_SIZE);
        memset(page_table, 0, PAGE_SIZE);                   // 经专区清零新表

        page_directory[directory_index] = table | PAGE_PRESENT | PAGE_WRITE | (flags & PAGE_USER);
        mi_core_table_slot[row][directory_index] = (uint16_t)slot;
        miCoreInvalidatePage(virtual);
    } else {
        // PageTable 已经存在, 经专区取出它
        // repair: 页表已存在时也要把 PDE 的 U 位补上 —— ring3 能不能访问由 PDE 和 PTE
        // repair: 两级共同决定, 只要一级没 U 就会保护性缺页(错误码 P=1,U/S=1)。
        if (flags & PAGE_USER) {
            page_directory[directory_index] |= PAGE_USER;
        }
        page_table = (uint32_t*)(PAGE_TABLE_WINDOW_BEGIN +
                                 mi_core_table_slot[row][directory_index] * PAGE_SIZE);
    }

    // 根据 PTE 索引写入物理页地址和页属性，完成映射
    page_table[table_index] = (physical & 0xFFFFF000) | flags;
    miCoreInvalidatePage(virtual);
}


// 删除虚拟地址到物理地址的映射
void miCorePageMapErase(uint32_t directory, uint32_t virtual) {
    // 专区那段地址不参与普通映射删除
    if (virtual >= PAGE_TABLE_WINDOW_BEGIN && virtual < PAGE_TABLE_WINDOW_END) {
        return;
    }

    uint32_t* page_directory;
    int row = miCorePageDirectoryRow(directory, &page_directory);

    if (row < 0) {
        return;
    }

    uint32_t directory_index = virtual >> 22;
    uint32_t table_index = (virtual >> 12) & 0x3FF;

    // 如果对应的 PageTable 不存在，则没有需要删除的映射
    if ((page_directory[directory_index] & PAGE_PRESENT) == 0) {
        return;
    }

    // 找到对应 PageTable(经专区)，并清除指定 PTE
    uint32_t* page_table =
        (uint32_t*)(PAGE_TABLE_WINDOW_BEGIN +
                    mi_core_table_slot[row][directory_index] * PAGE_SIZE);

    page_table[table_index] = 0;
    miCoreInvalidatePage(virtual);
}


// 设置bitmap位， 1占用，0未占用
static void setMapToTarget(size_t idx, int target) {
    uint32_t mask = 1u << (idx % 32);

    if (target) {

        mi_core_memory_bit_map[idx / 32] |= mask;
    } else {
        mi_core_memory_bit_map[idx / 32] &= ~mask;
    }
}

// 前4MB已经被分配过, 初始化
int miCoreMemoryInit() {
    int length = MI_CORE_INIT_MEMORY_SIZE;
    for (int i = 0; i < length; i++) {
        setMapToTarget(i, 1);
    }
    mi_core_memory_min_begin_alloc = length;
    return length;
}


// 分配物理内存页, 0失败， 其他为地址
uint32_t miCoreMemoryAllocPage() {
    size_t total = MI_CORE_MEMORY_TOTAL_SIZE * 32;

    for (size_t idx = mi_core_memory_min_begin_alloc; idx < total; idx++) {
        uint32_t mask = 1u << (idx % 32);

        if ((mi_core_memory_bit_map[idx / 32] & mask) == 0) {
            setMapToTarget(idx, 1);

            mi_core_memory_min_begin_alloc = idx + 1;

            return idx * MI_CORE_PAGE_SIZE;
        }
    }

    return 0;
}


int miCoreMemoryFreePage(uint32_t addr) {
    if (addr % MI_CORE_PAGE_SIZE != 0) {
        return -1;
    }

    size_t idx = addr / MI_CORE_PAGE_SIZE;

    if (idx < MI_CORE_INIT_MEMORY_SIZE) {
        return -1;
    }

    size_t total = MI_CORE_MEMORY_TOTAL_SIZE * 32;

    if (idx >= total) {
        return -1;
    }

    // 检查addr合法性
    uint32_t mask = 1u << (idx % 32);
    if ((mi_core_memory_bit_map[idx / 32] & mask) == 0) {
        return -1;
    }

    setMapToTarget(idx, 0);
    if (idx < mi_core_memory_min_begin_alloc) {
        mi_core_memory_min_begin_alloc = idx;
    }

    return 0;
}


// 创建一张干净的顶级页表(页目录):
//   1. 分配一个物理页作为新页目录
//   2. 在 900MB-1GB 专区里给它一个虚拟槽位, 方便内核读写
//   3. 清零后复制当前页目录的前 1GB 内核 PDE
//   4. 返回物理地址, 调用方可以直接拿去写 CR3
phys_addr_type miCoreCreateTopPageDirectory() {
    if (mi_core_pd_count >= MI_CORE_MAX_PAGE_DIRECTORY) {
        return 0;
    }

    // 复制期间不能让进程切走, 先把中断关掉, 结束时恢复原 EFLAGS
    uint32_t eflags;
    asm volatile("pushf; popl %0; cli" : "=r"(eflags) :: "memory");

    phys_addr_type pd_phys = miCoreMemoryAllocPage();

    if (pd_phys == 0) {
        asm volatile("pushl %0; popf" :: "r"(eflags) : "memory", "cc");
        return 0;
    }

    // 给新页目录一个专区槽位, 这样 4MB 以上的页目录也能被内核访问
    size_t slot = miArenaAllocSlot();

    if (slot == 0) {
        miCoreMemoryFreePage(pd_phys);
        asm volatile("pushl %0; popf" :: "r"(eflags) : "memory", "cc");
        return 0;
    }

    miArenaSetSlot(slot, pd_phys);

    uint32_t* new_pd = (uint32_t*)(PAGE_TABLE_WINDOW_BEGIN + slot * PAGE_SIZE);

    // 先清空用户部分, 再复制 0-1GB 的内核 PDE
    memset(new_pd, 0, PAGE_SIZE);

    uint32_t* old_pd;
    int old_row = miCorePageDirectoryRow(miCoreGetCR3(), &old_pd);

    if (old_row < 0) {
        miArenaClearSlot(slot);
        miArenaFreeSlot(slot);
        miCoreMemoryFreePage(pd_phys);
        asm volatile("pushl %0; popf" :: "r"(eflags) : "memory", "cc");
        return 0;
    }

    for (size_t i = 0; i < 256; i++) {
        new_pd[i] = old_pd[i];
    }

    // 记下物理地址和虚拟别名, 并把内核 PDE 对应的槽位也复制过来
    size_t row = mi_core_pd_count + 1;
    memset(mi_core_table_slot[row], 0, sizeof(mi_core_table_slot[row]));

    for (size_t i = 0; i < 256; i++) {
        mi_core_table_slot[row][i] = mi_core_table_slot[old_row][i];
    }

    mi_core_pd_phys[mi_core_pd_count] = pd_phys;
    mi_core_pd_virt[mi_core_pd_count] = (addr_type)new_pd;
    mi_core_pd_count++;

    asm volatile("pushl %0; popf" :: "r"(eflags) : "memory", "cc");

    return pd_phys;
}


// 查顶级页表在专区里的虚拟别名: 只查一级页目录, 不涉及二级页表
addr_type miCoreGetPageDirectoryVirt(phys_addr_type pd_phys) {
    for (size_t i = 0; i < mi_core_pd_count; i++) {
        if (mi_core_pd_phys[i] == pd_phys) {
            return mi_core_pd_virt[i];
        }
    }

    return 0;
}


// 取某个页目录下, 指定 PDE 对应的二级页表槽位; 0 表示没有
size_t miCoreGetPageTableSlot(addr_type pd_virt, uint32_t pde) {
    uint32_t* page_directory;
    int row = miCorePageDirectoryRow(pd_virt, &page_directory);

    if (row < 0 || pde >= 1024) {
        return 0;
    }

    return mi_core_table_slot[row][pde];
}


// 释放某个 PDE 的二级页表槽位(只归还槽位, 不释放页表物理页和 PDE)
void miCoreReleasePageTableSlot(addr_type pd_virt, uint32_t pde) {
    uint32_t* page_directory;
    int row = miCorePageDirectoryRow(pd_virt, &page_directory);

    if (row < 0 || pde >= 1024) {
        return;
    }

    size_t slot = mi_core_table_slot[row][pde];

    if (slot == 0) {
        return;
    }

    miArenaClearSlot(slot);
    miArenaFreeSlot(slot);
    mi_core_table_slot[row][pde] = 0;
}


// 销毁一张由 miCoreCreateTopPageDirectory 创建的页目录:
// 归还它的专区槽位, 释放页目录物理页, 并从记录表里删掉
int miCoreDestroyTopPageDirectory(phys_addr_type pd_phys) {
    if (pd_phys == 0 || pd_phys == miCoreGetCR3()) {
        return -1;
    }

    for (size_t i = 0; i < mi_core_pd_count; i++) {
        if (mi_core_pd_phys[i] != pd_phys) {
            continue;
        }

        addr_type pd_virt = mi_core_pd_virt[i];
        size_t slot = (pd_virt - PAGE_TABLE_WINDOW_BEGIN) / PAGE_SIZE;

        miArenaClearSlot(slot);
        miArenaFreeSlot(slot);

        if (miCoreMemoryFreePage(pd_phys) != 0) {
            return -1;
        }

        size_t last = mi_core_pd_count - 1;

        if (i != last) {
            mi_core_pd_phys[i] = mi_core_pd_phys[last];
            mi_core_pd_virt[i] = mi_core_pd_virt[last];
            memcpy(mi_core_table_slot[i + 1],
                   mi_core_table_slot[last + 1],
                   sizeof(mi_core_table_slot[i + 1]));
        }

        memset(mi_core_table_slot[last + 1], 0,
               sizeof(mi_core_table_slot[last + 1]));
        mi_core_pd_count--;
        return 0;
    }

    return -1;
}
