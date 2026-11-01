/* test/ 专用的链接脚本
 *
 * 所有测试程序都链接到 USER_CODE_VADDR = 0x40000000, 这样 objcopy -O binary 出来的
 * 裸二进制, 偏移 0 就正好是入口 _start —— 这正是 miCoreProgramLoader 的假设:
 * 它把文件【原样】memcpy 到 0x40000000, 然后 enter_user_mode(0x40000000, ...)。
 *
 * 注意: 0x40000000 必须和 code/kernel/proc/kfork.h 里的 USER_CODE_VADDR 保持一致,
 *       改一处必须改另一处。
 */
ENTRY(_start)

SECTIONS
{
    . = 0x40000000;

    .text : {
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }
}
