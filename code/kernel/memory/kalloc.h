//
// Created by koddnty on 2026/11/1.
// kernel alloc内存分配模块
//

#pragma once

#include <stddef.h>

// 从内核堆分配/释放, 按 4KB 页对齐
void* miCoreKernelAllocPage(size_t size);
void miCoreKernelFreePage(void* ptr);
