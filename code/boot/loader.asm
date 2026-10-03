LOADER_BASE_ADDR    equ 0x08000
; repair: 原来是 0x10000，但内核 .bss(页位图 128KB) 会一直长到 0x36700，
; repair: 把页目录 0x20000 / 页表 0x21000 / 专区根表 0x22000 全压掉。搬到 1MB。
KERNEL_BASE_ADDR    equ 0x100000

KERNEL_SECTOR_BEGIN equ 20h
KERNEL_SECTOR_COUNT equ 010h



; 宏定义 ----------------------------------------
DA_32 EQU 4000h         ; 32位代码段属性
DA_C EQU 98h            ; 只执行代码段属性
DA_DRW EQU 92h          ; 可读写数据段属性
DA_DRWA EQU 93h          ; 存在的已访问的可读写数据段属性
DA_LIMIT_4K EQU 8000h

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
; 平坦数据段：基址 0、界限 4GB，专门用来按物理地址访问页表
PM_DESC_FLAT_DATA32:        Descriptor  0,          0FFFFFh,                      DA_DRW + DA_32 + DA_LIMIT_4K
; repair: 新增平坦【代码】段：跳内核时 CS 必须换成基址 0 的段，否则
; repair: loader 的 CS 基址是 PM_LOADER_CODE32(≈0x83xx)，同样一条 jmp 0x10000
; repair: 会跑到线性地址 0x83xx+0x10000 去，而内核其实被读在物理 0x10000。
PM_DESC_FLAT_CODE32:        Descriptor  0,          0FFFFFh,                      DA_C + DA_32 + DA_LIMIT_4K
; end of gdt define
GdtLen equ $ - PM_GDT
GdtPtr dw GdtLen - 1
    dd 0        ; gdt 基址

; gdt selector ----------------------------------------
SelectorLoaderCode32  equ PM_DESC_LOADER_CODE32  - PM_GDT
SelectorLoaderData32  equ PM_DESC_LOADER_DATA32  - PM_GDT
SelectorLoaderStack32 equ PM_DESC_LOADER_STACK32 - PM_GDT
SelectorLoaderVideo   equ PM_DESC_LOADER_VIDEO   - PM_GDT
SelectorFlatData32    equ PM_DESC_FLAT_DATA32    - PM_GDT
SelectorFlatCode32    equ PM_DESC_FLAT_CODE32    - PM_GDT

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


    ; 初始化虚拟内存 --------------------
    mov ax, SelectorFlatData32
    mov ds, ax
    mov es, ax
    mov fs, ax
    ; 置空pagedir
    mov edi, PageDirectory
    mov ecx, 1024
    xor eax, eax
.ClearPageDirectory:
    mov [edi], eax
    add edi, 4
    loop .ClearPageDirectory

    ; 初始化第一个页表
    mov edi, PageTable
    mov eax, 0x00000003       ; 第一个物理页地址 0 + Present + RW
    mov ecx, 1024
.InitPageTable:
    mov [edi], eax
    add eax, 0x1000         ; 下一个 PTE 对应下一个 4KB 物理页
    add edi, 4              ; 下一个 PTE
    loop .InitPageTable

    ; 设置 Page Directory[0]
    mov eax, PageTable
    or eax, 0x003
    mov [PageDirectory], eax

    ; 设置cr3， cr0
    mov eax, PageDirectory
    mov cr3, eax

    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    ; 测试虚拟内存
    mov byte [gs:0], 'A'
    mov byte [gs:1], 0x07

    ; 加载内核程序
    push dword KERNEL_SECTOR_COUNT
    push dword KERNEL_BASE_ADDR
    push dword KERNEL_SECTOR_BEGIN
    call miReadSector

    ; 进入内核（远跳转：先把 CS 换成基址 0 的平坦代码段，跳到的才是物理 0x10000）
    jmp dword SelectorFlatCode32:KERNEL_BASE_ADDR
    jmp $

%define MI_READSECTOR_NO_SECTION
%include "code/minFs/readSector.asm"

LOADER_CODE32_LEN equ $ - PM_LOADER_CODE32


; 页表 ----------------------------------------
PageDirectory equ 0x20000
PageTable     equ 0x21000

