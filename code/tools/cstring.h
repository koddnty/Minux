#pragma once


#include <stdbool.h>

// 比较字符串，相等返回0
// repair: 这个谓词（true=相等）和 strcmp 语义重复，cstring.c 里已经有标准 strcmp
// repair: （0=相等，负数/正数表示大小）了，新代码优先用 strcmp；本函数保留只为兼容。
bool miToolsStrcmp(const char* a, const char* b);
