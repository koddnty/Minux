/* 内核链接脚本
 *   ld -m elf_i386 -T code/kernel/kernel.ld -o kernel.elf <objs>
 *
 * 它做三件事：
 *   1) 把内核放在 0x100000（必须和 loader.asm 的 KERNEL_BASE_ADDR 一致）
 *      内核 .bss 有 128KB 的页位图，放 0x10000 会一路压到页目录 0x20000 上
 *   2) 安排 .text/.rodata/.data 的顺序
 *   3) 给出 __bss_start / __bss_end 两个符号，让入口代码能把 .bss 清零
 *      （objcopy -O binary 只导出已初始化的段，.bss 不在文件里，
 *        所以内核里任何全局/静态变量都得靠这一步清零）
 */

ENTRY(kernel_entry)

SECTIONS
{
    . = 0x100000;      /* 必须和 loader.asm 的 KERNEL_BASE_ADDR 一致 */

    .text : {
        *(.text*)
        *(.code32*)      /* lib/kmemory.asm 用的是 [section .code32]，归到 .text 里 */
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    __bss_start = .;
    .bss : {
        *(.bss*)
        *(COMMON)
    }
    __bss_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
