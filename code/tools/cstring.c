#include "cstring.h"

#include <stddef.h>

bool miToolsStrcmp(const char* a, const char* b) {
    int idx = 0;
    while (a[idx] == b[idx]) {
        if (a[idx] == '\0') {
            return true;
        }
        idx ++;
    }
    return false;
}


size_t strlen(const char* s) {
    size_t n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

int strcmp(const char* a, const char* b) {
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    /* 按 unsigned char 比较，避免高位字符被当成负数 */
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

char* strcpy(char* dst, const char* src) {
    char* d = dst;
    while ((*d++ = *src++) != '\0') {
    }
    return dst;
}

void* memcpy(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}

void* memset(void* dst, int c, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    for (size_t i = 0; i < n; i++) {
        d[i] = (unsigned char)c;
    }
    return dst;
}
