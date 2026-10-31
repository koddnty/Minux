#include "kmemory.h"




// 内存映射map
static uint32_t mi_core_memory_bit_map [MI_CORE_MEMORY_TOTAL_SIZE]; // 2 * 5 * 3 * 2K = 2^12 K = 4G 单位4K
static size_t mi_core_memory_min_begin_alloc = 0;           // 最小位可用内存

// 页表专区: 一张页表 = 专区里的一个槽位, 槽位 0 是 0~4MB 恒等映射那张表(0x21000)
static size_t mi_core_min_begin_virtual_table_alloc = 1;    // 专区下一个空闲槽位
static uint32_t mi_core_table_slot[1024] = {0};             // PDE 索引 -> 槽位(默认 0, 即 0~4MB 那张表)

// 专区的第 block 块(4MB)由哪张表管: 第 0 块是根表, 第 b(b>=1) 块放上一块最后一个槽位
static uint32_t* miArenaBlockTable(size_t block) {
    if (block == 0) {
        return (uint32_t*)PAGE_TABLE_ADDR;                  // 根表在低内存, 恒等可见
    }
    return (uint32_t*)(PAGE_TABLE_WINDOW_BEGIN + (block * 1024 - 1) * PAGE_SIZE);
}

// 按需把专区扩出第 block 块, 用多少映射多少
static void miArenaEnsureBlock(size_t block) {
    uint32_t* page_directory = (uint32_t*)miCoreGetCR3();
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
    mi_core_table_slot[pde] = slot;                         // 长出来的块也要记下槽位, 否则以后写错表
    miCoreInvalidatePage(PAGE_TABLE_WINDOW_BEGIN + block * PAGE_TABLE_VIRTUAL_SIZE);
}

// 取一个空闲槽位: 每块最后一个槽位要留给下一块的表, 跳过
static size_t miArenaAllocSlot(void) {
    while ((mi_core_min_begin_virtual_table_alloc + 1) % 1024 == 0) {
        mi_core_min_begin_virtual_table_alloc++;
    }
    return mi_core_min_begin_virtual_table_alloc++;
}

// 把槽位映射到物理页(写的是专区表, 永远可写)
static void miArenaSetSlot(size_t slot, uint32_t physical) {
    miArenaEnsureBlock(slot / 1024);
    miArenaBlockTable(slot / 1024)[slot % 1024] =
        (physical & 0xFFFFF000) | PAGE_PRESENT | PAGE_WRITE;
    miCoreInvalidatePage(PAGE_TABLE_WINDOW_BEGIN + slot * PAGE_SIZE);
}

// 初始化页表专区: 槽位 0 指向 0~4MB 那张表(内容不动), 根表挂到 PDE
int miCorePageArenaInit() {
    uint32_t* root = (uint32_t*)PAGE_TABLE_ADDR;             // 低内存, 可直接写
    uint32_t* page_directory = (uint32_t*)miCoreGetCR3();

    memset(root, 0, PAGE_SIZE);
    root[0] = PAGE_TABLE_LOW_ADDR | PAGE_PRESENT | PAGE_WRITE;
    page_directory[PAGE_TABLE_WINDOW_BEGIN >> 22] = PAGE_TABLE_ADDR | PAGE_PRESENT | PAGE_WRITE;
    miCoreInvalidatePage(PAGE_TABLE_WINDOW_BEGIN);
    return 0;
}

void miCorePageFaultHandler(uint32_t fault_addr) {
    uint32_t virtual_addr =
        fault_addr & ~(MI_CORE_PAGE_SIZE - 1);

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
    // 获取页目录，并根据虚拟地址计算 PDE 和 PTE 的索引
    uint32_t* page_directory = (uint32_t*)directory;

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
        uint32_t table = miCoreMemoryAllocPage();           // 页表的物理页(不用能直接访问)

        if (table == 0) {
            return;
        }

        miArenaSetSlot(slot, table);                        // 槽位 -> 物理页
        page_table = (uint32_t*)(PAGE_TABLE_WINDOW_BEGIN + slot * PAGE_SIZE);
        memset(page_table, 0, PAGE_SIZE);                   // 经专区清零新表

        page_directory[directory_index] = table | PAGE_PRESENT | PAGE_WRITE | (flags & PAGE_USER);
        mi_core_table_slot[directory_index] = slot;         // 记下 PDE -> 槽位, 删除时要用
        miCoreInvalidatePage(virtual);
    } else {
        // PageTable 已经存在, 经专区取出它
        // repair: 页表已存在时也要把 PDE 的 U 位补上 —— ring3 能不能访问由 PDE 和 PTE
        // repair: 两级共同决定, 只要一级没 U 就会保护性缺页(错误码 P=1,U/S=1)。
        if (flags & PAGE_USER) {
            page_directory[directory_index] |= PAGE_USER;
        }
        page_table = (uint32_t*)(PAGE_TABLE_WINDOW_BEGIN + mi_core_table_slot[directory_index] * PAGE_SIZE);
    }

    // 根据 PTE 索引写入物理页地址和页属性，完成映射
    page_table[table_index] = (physical & 0xFFFFF000) | flags;
    miCoreInvalidatePage(virtual);
}


// 删除虚拟地址到物理地址的映射
void miCorePageMapErase(uint32_t directory, uint32_t virtual) {
    // 获取页目录，并根据虚拟地址计算 PDE 和 PTE 的索引
    uint32_t* page_directory = (uint32_t*)directory;

    uint32_t directory_index = virtual >> 22;
    uint32_t table_index = (virtual >> 12) & 0x3FF;

    // 如果对应的 PageTable 不存在，则没有需要删除的映射
    if ((page_directory[directory_index] & PAGE_PRESENT) == 0) {
        return;
    }

    // 找到对应 PageTable(经专区)，并清除指定 PTE
    uint32_t* page_table =
        (uint32_t*)(PAGE_TABLE_WINDOW_BEGIN + mi_core_table_slot[directory_index] * PAGE_SIZE);

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
