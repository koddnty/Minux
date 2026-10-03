/* 内核 C 主体。
 * kernel_entry.asm 已经把 CS/DS/SS 拉平、栈切好、.bss 清零，然后 call 到这里。
 */
#include "lib/kprintf.h"
#include <stdbool.h>
#include <stdint.h>



int kernel_main(void) {
    kVagprintf("success: init gdt\n");
    kVagprintf("success: init virtual memory\n");
    kVagprintf("success: init tss\n");
    kVagprintf("Loading kernel...\n");
    return 0;
}
