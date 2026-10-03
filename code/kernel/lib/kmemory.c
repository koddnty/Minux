#include "kmemory.h"




// 内存映射map
static uint32_t mi_core_memory_bit_map [MI_CORE_MEMORY_TOTAL_SIZE]; // 2 * 5 * 3 * 2K = 2^12 K = 4G 单位4K
static size_t mi_core_memory_min_begin_alloc = 0;           // 最小位可用内存
static uint32_t mi_core_min_begin_virtual_table_alloc = 0;      // 单位为table个数。

#include "kmemory.h"

#define PAGE_PRESENT 0x001
#define PAGE_WRITE   0x002
#define PAGE_USER    0x004

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

    // 如果对应的 PageTable 不存在，则分配一个物理页作为 PageTable
    uint32_t page_table_addr;

    if ((page_directory[directory_index] & PAGE_PRESENT) == 0) {
        page_table_addr = miCoreMemoryAllocPage();

        if (page_table_addr == 0) {
            return;
        }

        // 新 PageTable 清零，并将它挂到对应的 PDE 上
        memset((void*)page_table_addr, 0, MI_CORE_PAGE_SIZE);

        page_directory[directory_index] =
            page_table_addr |
            PAGE_PRESENT |
            PAGE_WRITE |
            (flags & PAGE_USER);
    } else {
        // PageTable 已经存在，直接取出它的物理地址
        page_table_addr =
            page_directory[directory_index] & 0xFFFFF000;
    }

    // 根据 PTE 索引写入物理页地址和页属性，完成映射
    uint32_t* page_table = (uint32_t*)page_table_addr;

    page_table[table_index] =
        (physical & 0xFFFFF000) | flags;
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

    // 找到对应 PageTable，并清除指定 PTE
    uint32_t page_table_addr =
        page_directory[directory_index] & 0xFFFFF000;

    uint32_t* page_table = (uint32_t*)page_table_addr;

    page_table[table_index] = 0;
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