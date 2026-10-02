#include "kprintf.h"

#include <stdarg.h>

#define VIDEO_MEM   ((volatile uint8_t*)0xB8000)
#define COL_MAX     80
#define ROW_MAX     25

/* 光标：从第 2 行开始（第 1 行留给 loader 写的 'A' / " MBR"） */
static uint32_t kRow = 0;
static uint32_t kCol = 0;

void kScroll(uint32_t n) {
    if (n == 0) {
        if (kRow < ROW_MAX) {
            return;
        }

        n = kRow - ROW_MAX + 1;
    }

    if (n >= ROW_MAX) {
        n = ROW_MAX;
    }

    for (uint32_t r = 0; r < ROW_MAX - n; r++) {
        for (uint32_t c = 0; c < COL_MAX; c++) {
            uint32_t dst = (r * COL_MAX + c) * 2;
            uint32_t src = ((r + n) * COL_MAX + c) * 2;

            VIDEO_MEM[dst] = VIDEO_MEM[src];
            VIDEO_MEM[dst + 1] = VIDEO_MEM[src + 1];
        }
    }

    for (uint32_t r = ROW_MAX - n; r < ROW_MAX; r++) {
        for (uint32_t c = 0; c < COL_MAX; c++) {
            uint32_t p = (r * COL_MAX + c) * 2;

            VIDEO_MEM[p] = ' ';
            VIDEO_MEM[p + 1] = 0x07;
        }
    }

    if (kRow >= n) {
        kRow -= n;
    } else {
        kRow = 0;
    }
}

void kclear(void) {
    for (uint32_t i = 0; i < COL_MAX * ROW_MAX; i++) {
        VIDEO_MEM[i * 2] = ' ';
        VIDEO_MEM[i * 2 + 1] = 0x07;
    }
    kRow = 1;
    kCol = 0;
}

void kputc(char c) {
    if (c == '\n') {
        kCol = 0;
        kRow++;
        kScroll(0);
        return;
    }

    if (c == '\r') {
        kCol = 0;
        return;
    }

    uint32_t p = (kRow * COL_MAX + kCol) * 2;
    VIDEO_MEM[p] = (uint8_t)c;
    VIDEO_MEM[p + 1] = 0x07;

    if (++kCol >= COL_MAX) {
        kCol = 0;
        kRow++;
        kScroll(0);
    }
}

void kputs(const char* s) {
    while (*s) {
        kputc(*s++);
    }
}

static void kPrintUint(uint32_t v, uint32_t base, int negative) {
    char buf[36];
    int i = 0;

    if (negative) {
        kputc('-');
    }

    if (v == 0) {
        kputc('0');
        return;
    }

    while (v > 0 && i < (int)sizeof(buf)) {
        uint32_t d = v % base;
        buf[i++] = (char)(d < 10 ? '0' + d : 'a' + (d - 10));
        v /= base;
    }

    while (i-- > 0) {
        kputc(buf[i]);
    }
}

void kprintf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    for (const char* p = fmt; *p; p++) {
        if (*p != '%') {
            kputc(*p);
            continue;
        }

        p++;
        switch (*p) {
        case '%':
            kputc('%');
            break;
        case 'c':
            kputc((char)va_arg(ap, int));
            break;
        case 's': {
            const char* s = va_arg(ap, const char*);
            kputs(s ? s : "(null)");
            break;
        }
        case 'd': {
            int v = va_arg(ap, int);
            kPrintUint((uint32_t)(v < 0 ? -v : v), 10, v < 0);
            break;
        }
        case 'u':
            kPrintUint(va_arg(ap, uint32_t), 10, 0);
            break;
        case 'x':
            kPrintUint(va_arg(ap, uint32_t), 16, 0);
            break;
        default:                    /* 不认识的格式符原样打出来，方便发现笔误 */
            kputc('%');
            kputc(*p);
            break;
        }
    }

    va_end(ap);
}
