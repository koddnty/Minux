#pragma once

#include <stdint.h>

#define MI_SECTOR_SIZE 512

/* 返回值约定（和 code/minFs 保持一致）：失败 -1，成功 0 或正数。
   判断失败用 ret < 0 / ret != 0。 */

int miDiskOpen(const char* path);      /* 成功 0，失败 -1 */
void miDiskClose(void);

int miReadSector(uint32_t lba, void* buffer, uint32_t count);        /* 成功 0，失败 -1 */
int miWriteSector(uint32_t lba, const void* buffer, uint32_t count); /* 成功 0，失败 -1 */
