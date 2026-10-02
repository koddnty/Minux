; ===========================================================================
;  内核入口（32 位保护模式）
;  loader 已经把内核读到 KERNEL_BASE_ADDR 并跳过来了，这里负责：
;    1) 用【内核自己的】GDT 重载 CS/DS/SS（不再依赖 loader 的 GDT 布局）
;    2) 切到自己的栈
;    3) 清零 .bss（objcopy 出来的裸二进制不含 .bss，不清就是垃圾值）
;    4) 调用 C 的 kmain
;  nasm -f elf32 kernel_entry.asm -o kernel_entry.o
; ===========================================================================
[BITS 32]

global kernel_entry
extern kernel_main
extern __bss_start
extern __bss_end

KERNEL_STACK_SIZE equ 8192

section .text
kernel_entry:
    cli
    cld

    ; 1) 加载自己的 GDT，然后远跳转重载 CS
    lgdt [gdt_desc]
    jmp SEL_CODE:.reload_cs

.reload_cs:
    ; 2) 数据段全部拉平（基址 0、界限 4G）
    mov ax, SEL_DATA
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; 3) 自己的栈
    mov esp, stack_top
    mov ebp, esp

    ; 4) 清 .bss
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    ; 5) 进 C
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang


; ---- 内核自己的平坦 GDT ----------------------------------------------------
section .data
align 8
gdt:
    dq 0                                        ; 0x00 空描述符
gdt_code32:
    dq 0x00CF9A000000FFFF                       ; 0x08 基址0 界限4G 32位代码
gdt_data32:
    dq 0x00CF92000000FFFF                       ; 0x10 基址0 界限4G 32位数据
gdt_end:
gdt_desc:
    dw gdt_end - gdt - 1
    dd gdt

SEL_CODE equ 0x08
SEL_DATA equ 0x10


; ---- 内核栈（放 .bss，不占二进制体积；由上面第 4 步清零） -------------------
section .bss
align 16
stack_bottom:
    resb KERNEL_STACK_SIZE
stack_top:
