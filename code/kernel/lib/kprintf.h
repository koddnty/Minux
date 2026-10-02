#pragma once

#include <stdint.h>

/* 极简内核输出：直接写 0xB8000 的文本显存，不依赖任何库。
 * 光标从第 2 行开始，这样 loader 写的 'A' / " MBR" 留在第一行，方便对照。
 */
void kVagScroll(uint32_t n);
void kVagclear(void);
void kVagputs(const char* s);
void kVagputc(char c);

/* 支持 %c %s %d %u %x %% 和 \n（换行 + 回车） */
void kVagprintf(const char* fmt, ...);
