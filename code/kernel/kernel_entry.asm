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

; 宏定义 ----------------------------------------
DA_32 EQU 4000h         ; 32位代码段属性
DA_C EQU 98h            ; 只执行代码段属性
DA_DRW EQU 92h          ; 可读写数据段属性
DA_DRWA EQU 93h          ; 存在的已访问的可读写数据段属性
DA_LIMIT_4K EQU 8000h
DA_DPL3 EQU 60h             ; ring3特权级


%macro Descriptor 3
    dw %2 & 0FFFFh
    dw %1 & 0FFFFh
    db (%1 >> 16) & 0FFh
    db (%3) & 0FFh
    db ((%2 >> 16) & 0Fh) | (((%3) >> 8) & 0F0h)
    db (%1 >> 24) & 0FFh
%endmacro


; tss表结构

; tss表
struc TSS32
    .prev_task_link resw 1
    .reserved0      resw 1
    .esp0           resd 1
    .ss0            resw 1
    .reserved1      resw 1
    .esp1           resd 1
    .ss1            resw 1
    .reserved2      resw 1
    .esp2           resd 1
    .ss2            resw 1
    .reserved3      resw 1
    .cr3            resd 1
    .eip            resd 1
    .eflags          resd 1
    .eax            resd 1
    .ecx            resd 1
    .edx            resd 1
    .ebx            resd 1
    .esp            resd 1
    .ebp            resd 1
    .esi            resd 1
    .edi            resd 1
    .es             resw 1
    .reserved4      resw 1
    .cs             resw 1
    .reserved5      resw 1
    .ss             resw 1
    .reserved6      resw 1
    .ds             resw 1
    .reserved7      resw 1
    .fs             resw 1
    .reserved8      resw 1
    .gs             resw 1
    .reserved9      resw 1
    .ldt            resw 1
    .reserved10     resw 1
    .trap           resb 1
    .iomap_base     resw 1
endstruc


global kernel_entry
extern kernel_main
extern __bss_start
extern __bss_end

; 缺页中断
global page_fault_entry
extern miCorePageFaultHandler

KERNEL_STACK_SIZE equ 8192
PROCESS_STACK_SIZE equ 8192

[SECTION .text]
kernel_entry:
    cli
    cld

    ; 1) 加载自己的 GDT，然后远跳转重载 CS
    lgdt [GdtPtr]
    jmp SelectorKernelCode32:kernel_reload_cs


; 缺页中断处理函数入口
; repair: 错误码 bit2 = U/S(0=内核态访问)。内核态缺页基本都是真 bug,
; repair: 交给"按需分页"会一直补页形成风暴(调试时极难定位), 这里直接停住。
page_fault_entry:
    test byte [esp], 0x04
    jz kernel_fault_panic

    push ds
    push es
    push fs
    push gs

    mov ax, SelectorKernelData32
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    pushad

    mov eax, cr2
    push eax
    call miCorePageFaultHandler
    add esp, 4

    popad

    pop gs
    pop fs
    pop es
    pop ds

    add esp, 4
    iretd



; repair: 原来是 .reload_cs 这个"局部标签"，但它上面插进了 page_fault_entry 这个非局部
; repair: 标签，于是 .reload_cs 挂到了 page_fault_entry 名下，kernel_entry.reload_cs 不存在
; repair: （报 symbol `kernel_entry.reload_cs' not defined）。改成普通标签。
kernel_fault_panic:
    cli
    hlt
    jmp kernel_fault_panic


kernel_reload_cs:
    ; 2) 数据段全部拉平（基址 0、界限 4G）
    mov ax, SelectorKernelData32
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; 3) 自己的栈
    mov esp, kernel_stack_top
    mov ebp, esp

    ; 4) 清 .bss
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb


    ; TSS 处理    ----------------------------------------
    ; 初始化tss位置
    mov eax, tss
    mov word [PM_DESC_TSS + 2], ax
    shr eax, 16
    mov byte [PM_DESC_TSS + 4], al
    mov byte [PM_DESC_TSS + 7], ah
    ; 初始化tss数据
    mov word [tss + TSS32.ss0], SelectorKernelData32
    mov dword [tss + TSS32.esp0], process_stack_top
    mov word [tss + TSS32.iomap_base], TSS32_size

    ; 加载tss
    mov ax, SelectorTSS
    ltr ax

    ; 回填 14 号(缺页)表项的处理函数地址: 汇编期拿不到重定位后的地址,
    ; 只能运行时填(和 loader 回填 GDT 基址一个套路)
    mov eax, page_fault_entry
    mov word [IDT + 14 * 8], ax
    shr eax, 16
    mov word [IDT + 14 * 8 + 6], ax

    lidt [IdtPtr]



    ; 缺页中断处理 ----------------------------------------
    ; 进 kernel C
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang




; gdt表  ----------------------------------------
[section .data]             ;           段基址       界限                            属性
[BITS 32]
PM_GDT:                     Descriptor  0,          0,                              0
PM_DESC_KERNEL_CODE32:      Descriptor  0,          0FFFFFh,            DA_C + DA_32 + DA_LIMIT_4K
PM_DESC_KERNEL_DATA32:      Descriptor  0,          0FFFFFh,            DA_DRW + DA_32 + DA_LIMIT_4K

PM_DESC_USER_CODE32:        Descriptor  0,          0FFFFFh,            DA_C + DA_32 + DA_LIMIT_4K + DA_DPL3

PM_DESC_USER_DATA32:        Descriptor  0,         0FFFFFh,            DA_DRW + DA_32 + DA_LIMIT_4K + DA_DPL3

PM_DESC_TSS:                Descriptor  0,         TSS32_size - 1,     89h
; end of gdt define
GdtLen equ $ - PM_GDT
GdtPtr:
    dw GdtLen - 1
    dd PM_GDT

; gdt selector ----------------------------------------
SelectorKernelCode32    equ PM_DESC_KERNEL_CODE32  - PM_GDT
SelectorKernelData32    equ PM_DESC_KERNEL_DATA32  - PM_GDT
SelectorUserCode32      equ PM_DESC_USER_CODE32  - PM_GDT
SelectorUserData32      equ PM_DESC_USER_DATA32  - PM_GDT
SelectorTSS             equ PM_DESC_TSS - PM_GDT




; IDT ----------------------------------------
; repair: 原来写的是 dw page_fault_entry & 0FFFFh / >> 16 —— nasm 不允许对可重定位
; repair: 的标签做 & 和 >>（报 "may only be applied to scalar values"），编不过。
; repair: 改成先留 0，运行时代码里回填（见上面 lidt 之前那几行）。
align 8
IDT:
    times 14 dq 0
    dw 0                    ; offset 15:0  运行时回填
    dw SelectorKernelCode32
    db 0
    db 10001110b
    dw 0                    ; offset 31:16 运行时回填
    times 241 dq 0
; end of IDT
IdtLen equ $ - IDT

IdtPtr:
    dw IdtLen - 1
    dd IDT




; ---- 内核栈（放 .bss，不占二进制体积；由上面第 4 步清零） -------------------
[section .bss]
align 16
tss:
    resb TSS32_size
kernel_stack_bottom:
    resb KERNEL_STACK_SIZE
kernel_stack_top:

process_stack_bottom:
    resb PROCESS_STACK_SIZE
process_stack_top:





