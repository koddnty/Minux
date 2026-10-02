#pragma once

/* repair: 内核用的最小 string.h。
 *
 * fsTree.h / fsTree.c 里写的是 #include <string.h>，而内核是 freestanding，
 * 没有 glibc 的 string.h，编译内核版 fsTree.c 时会直接 fatal error。
 * 这里用「标准函数名」补一份，好处是 fsTree.c 一行都不用改。
 *
 * 用法：只在【内核】构建时把这个目录加进 include 路径（-I code/tools）；
 *      宿主编译请不要加，否则会把 glibc 的 <string.h> 遮掉。
 *
 * 注意：gcc 在 -ffreestanding 下仍然自带 <stddef.h>/<stdint.h> 等编译器头，
 *      所以这里可以直接用 size_t；但不要加 -nostdinc（那会把这些也屏蔽掉）。
 */

#include <stddef.h>

/* 注意：这几个是标准库语义，不适用本项目的「失败负数」约定：
 *   strlen -> 长度（>=0）
 *   strcmp -> 相等返回 0、a<b 返回负、a>b 返回正（恰好也是"负数=小于"，别和错误码混）
 *   strcpy/memcpy/memset -> 返回目标指针
 * 项目里"失败返回负数"的约定只针对 miXxx 这些业务函数。 */
size_t strlen(const char* s);
int strcmp(const char* a, const char* b);
char* strcpy(char* dst, const char* src);
void* memcpy(void* dst, const void* src, size_t n);
void* memset(void* dst, int c, size_t n);
