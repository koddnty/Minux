#pragma once

#include "kmemory.h"

// 只给内核其他模块用的页表生命周期接口, 不对外暴露
size_t miCoreGetPageTableSlot(addr_type pd_virt, uint32_t pde);
void miCoreReleasePageTableSlot(addr_type pd_virt, uint32_t pde);
int miCoreDestroyTopPageDirectory(phys_addr_type pd_phys);

// 临时把任意物理页映射到页表专区的空闲槽位, 用于 fork 复制页面
addr_type miCoreMapPageTemp(phys_addr_type pa);
void miCoreUnmapPageTemp(addr_type va);
