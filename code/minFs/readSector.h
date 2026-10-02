#pragma once
#include <stdint.h>     /* repair: 原为 <cstdint>（C++ 头），用 gcc 编 C 会 fatal error */

/**
 * @brief 读取扇区(512B)数据
 * @param lba 起始扇区号
 * @param buffer 缓冲区
 * @param count     读入扇区数
 * @return 成功 0；失败 -1（全项目统一：失败负数、成功 0 或正数）
 *
 * 注意：宿主侧由 host/fsSys.c 实现、内核侧由 readSector.asm 实现，两边语义一致。
 */
extern int miReadSector(uint32_t lba, void* buffer, uint32_t count);
/* repair: 原为 void*，和 host/fsSys.h 的 const void* 冲突；
   同一个 TU 里同时 include 两个头会报 conflicting types for 'miWriteSector' */
extern int miWriteSector(uint32_t lba, const void* buffer, uint32_t count);