LOADER_BASE_ADDR    equ 0x08000
; 宏定义 ----------------------------------------
DA_32 EQU 4000h         ; 32位代码段属性
DA_C EQU 98h            ; 只执行代码段属性
DA_DRW EQU 92h          ; 可读写数据段属性
DA_DRWA EQU 93h          ; 存在的已访问的可读写数据段属性

%macro Descriptor 3
    dw %2 & 0FFFFh
    dw %1 & 0FFFFh
    db (%1 >> 16) & 0FFh
    db (%3) & 0FFh
    db ((%2 >> 16) & 0Fh) | (((%3) >> 8) & 0F0h)
    db (%1 >> 24) & 0FFh
%endmacro


; 实模式下，准备进入保护模式
; loader入口 ======================================== 1
[SECTION .s16]
org LOADER_BASE_ADDR
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov sp, 0100h       ; 栈顶
    ; 初始化32位代码段
    xor eax, eax
    mov ax, cs
    shl eax, 4
    add eax, PM_LOADER_CODE32
    mov word [PM_DESC_LOADER_CODE32 + 2], ax
    shl eax, 16
    mov byte [PM_DESC_LOADER_CODE32 + 4], al
    mov byte [PM_DESC_LOADER_CODE32 + 7], ah
    ; 初始化32位data段
    xor eax, eax
    mov ax, cs
    shl eax, 4
    add eax, PM_LOADER_DATA32
    mov word [PM_DESC_LOADER_DATA32 + 2], ax
    shl eax, 16
    mov byte [PM_DESC_LOADER_DATA32 + 4], al
    mov byte [PM_DESC_LOADER_DATA32 + 7], ah
    ; 初始化32位stack段
    xor eax, eax
    mov ax, cs
    shl eax, 4
    add eax, PM_LOADER_STACK32
    mov word [PM_DESC_LOADER_STACK32 + 2], ax
    shl eax, 16
    mov byte [PM_DESC_LOADER_STACK32 + 4], al
    mov byte [PM_DESC_LOADER_STACK32 + 7], ah

    ; 加载GDTR
    xor eax, eax
    mov ax, ds
    shl eax, 4
    add eax, PM_GDT
    mov dword [GdtPtr + 2], eax
    lgdt [GdtPtr]
    ; A20
    cli
    in al, 92h
    or al, 00000010b
    out 92h, al

    ; 进入保护模式
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword SelectorLoaderCode32:0



; gdt表  ----------------------------------------
[SECTION .gdt]              ;   段基址      界限                属性
[BITS 16]
PM_GDT:                     Descriptor  0,          0,                            0
PM_DESC_LOADER_CODE32:      Descriptor  0,          LOADER_CODE32_LEN - 1,        DA_32 | DA_C          ; loader代码段
PM_DESC_LOADER_DATA32:      Descriptor  0,          LOADER_DATA32_LEN_LEN - 1,    DA_DRW                ; loader 数据段
PM_DESC_LOADER_STACK32:     Descriptor  0,          LOADER_STACK32_TOP - 1,       DA_DRW + DA_32        ; loader 栈段
PM_DESC_LOADER_VIDEO:       Descriptor  0B8000h,    0ffffh,                       DA_DRW                ; loader 显示段
; end of gdt define
GdtLen equ $ - PM_GDT
GdtPtr dw GdtLen - 1
    dd 0        ; gdt 基址

; gdt selector ----------------------------------------
SelectorLoaderCode32  equ PM_DESC_LOADER_CODE32  - PM_GDT
SelectorLoaderData32  equ PM_DESC_LOADER_DATA32  - PM_GDT
SelectorLoaderStack32 equ PM_DESC_LOADER_STACK32 - PM_GDT
SelectorLoaderVideo   equ PM_DESC_LOADER_VIDEO   - PM_GDT

; loader 数据段 ----------------------------------------
[SECTION .data1]
align 32
[BITS 32]
PM_LOADER_DATA32:
PMMessage db "Potect Mode", 0
OffsetPMMessage equ PMMessage - $$
LOADER_DATA32_LEN_LEN equ $ - PM_LOADER_DATA32

; loader stack段 ----------------------------------------
[SECTION .gs]
ALIGN 32
[BITS 32]
PM_LOADER_STACK32:
    times 512 db 0
LOADER_STACK32_TOP equ $ - PM_LOADER_STACK32 - 1







; 保护模式入口 ======================================== 2
[SECTION .s32]
[BITS 32]
PM_LOADER_CODE32:
    ; 重新加载保护模式下的数据段
    mov ax, SelectorLoaderData32
    mov ds, ax
    mov es, ax
    mov fs, ax
    ; 重新加载保护模式下的栈段
    mov ax, SelectorLoaderStack32
    mov ss, ax
    mov esp, LOADER_STACK32_TOP
    ; 重新加载保护模式下的显存段
    mov ax, SelectorLoaderVideo
    mov gs, ax
    ; 测试保护模式显存访问
    mov byte [gs:0], 'A'
    mov byte [gs:1], 0x07
    jmp $
LOADER_CODE32_LEN equ $ - PM_LOADER_CODE32