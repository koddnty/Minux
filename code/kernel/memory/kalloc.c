#include "kalloc.h"

#include "kmemory.h"

#define KALLOC_MAX 256

typedef struct {
    addr_type addr;
    size_t size;
} KAllocEntry;

static KAllocEntry allocs[KALLOC_MAX];
static size_t alloc_count = 0;

static KAllocEntry free_blocks[KALLOC_MAX];
static size_t free_count = 0;

static addr_type heap_next = BEGIN_OF_CORE_HEAP;

static size_t alignPage(size_t size) {
    return (size + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
}

// 从内核堆分配一块按页对齐的内存, 物理页在第一次分配时建立映射
void* miCoreKernelAllocPage(size_t size) {
    size = alignPage(size);

    if (size == 0 || alloc_count >= KALLOC_MAX) {
        return 0;
    }

    // 优先复用同样大小的空闲块
    for (size_t i = 0; i < free_count; i++) {
        if (free_blocks[i].size != size) {
            continue;
        }

        KAllocEntry entry = free_blocks[i];
        free_blocks[i] = free_blocks[--free_count];
        allocs[alloc_count++] = entry;
        memset((void*)entry.addr, 0, entry.size);
        return (void*)entry.addr;
    }

    if (heap_next + size > END_OF_CORE_HEAP) {
        return 0;
    }

    addr_type base = heap_next;
    addr_type va = base;

    while (va < base + size) {
        uint32_t pa = miCoreMemoryAllocPage();

        if (pa == 0) {
            return 0;
        }

        miCorePageMap(
            miCoreGetCR3(),
            va,
            pa,
            PAGE_PRESENT | PAGE_WRITE
        );

        memset((void*)va, 0, PAGE_SIZE);
        va += PAGE_SIZE;
    }

    heap_next = base + size;

    allocs[alloc_count].addr = base;
    allocs[alloc_count].size = size;
    alloc_count++;

    return (void*)base;
}

// 释放回内核堆, 虚拟地址和物理页保留, 下次同大小分配直接复用
void miCoreKernelFreePage(void* ptr) {
    addr_type addr = (addr_type)ptr;

    for (size_t i = 0; i < alloc_count; i++) {
        if (allocs[i].addr != addr) {
            continue;
        }

        KAllocEntry entry = allocs[i];
        allocs[i] = allocs[--alloc_count];

        if (free_count < KALLOC_MAX) {
            free_blocks[free_count++] = entry;
        }

        return;
    }
}
