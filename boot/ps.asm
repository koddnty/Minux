DA_32 EQU 4000h         ; 32位代码段属性
DA_C EQU 98h            ; 只执行代码段属性
DA_DRW EQU 92h          ; 可读写数据段属性
DA_DRWA EQU 93h          ; 存在的已访问的可读写数据段属性


%macro Descriptor 3
    dw %2 & 0xFFFF      ; 段界限 15:0
    dw %1 & 0xFFFF      ; 段基址 15:0
    db (%1 >> 16) & 0xFF ; 段基址 23:16
    dw ((%2 >> 8) & 0xF00) | (%3 & 0F0FFh) ; 段界限 19:16 + 段属性
    db (%1 >> 24) & 0xFF ;
%endmacro



org 0x08000

mov ax, 0xb800
mov gs, ax
mov byte [gs:0x00], '2'
mov byte [gs:0x01], 0xA4

jmp PM_BEGIN


[SECTION .s16]
[BITS 16]
PM_BEGIN:
    mov ax, 0xb800
    mov gs, ax
    mov byte [gs:0x00], '3'
    mov byte [gs:0x01], 0xA4

    mov ax, cs
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov sp, 0100h
    ;; 接下来的部分设置GDT的base
    ; 初始化32位代码段
    xor eax, eax
    mov ax, cs
    shl eax, 4
    add eax, PM_SEG_CODE32
    mov word [PM_DESC_CODE32 + 2], ax
    shl eax, 16
    mov byte [PM_DESC_CODE32 + 4], al
    mov byte [PM_DESC_CODE32 + 7], ah
    ; 初始化32位data段
    xor eax, eax
    mov ax, cs
    shl eax, 4
    add eax, PM_DATA
    mov word [PM_DESC_DATA32 + 2], ax
    shl eax, 16
    mov byte [PM_DESC_DATA32 + 4], al
    mov byte [PM_DESC_DATA32 + 7], ah
    ; 初始化32位stack段
    xor eax, eax
    mov ax, cs
    shl eax, 4
    add eax, PM_STACK
    mov word [PM_DESC_STACK32 + 2], ax
    shl eax, 16
    mov byte [PM_DESC_STACK32 + 4], al
    mov byte [PM_DESC_STACK32 + 7], ah

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


    mov ax, 0xb800
    mov gs, ax
    mov byte [gs:0x00], '4'
    mov byte [gs:0x01], 0xA4


    ; 进入保护模式
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword SelectorCode32:PM_SEG_CODE32
    



[SECTION .gdt]              ;   段基址      界限                属性
PM_GDT:             Descriptor  0,          0,                  0
PM_DESC_CODE32:     Descriptor  0,          SegCode32Len - 1,   DA_32 | DA_C
PM_DESC_DATA32:     Descriptor  0,          DATALen - 1,        DA_DRW
PM_DESC_STACK32:    Descriptor  0,          TopOfStack - 1,     DA_DRW + DA_32
PM_DESC_TEST:       Descriptor  0200000h,  0ffffh,              DA_DRW
PM_DESC_VIDEO:      Descriptor  0B8000h,    0,                  DA_DRW
; end of defination gdt
GdtLen equ $ - PM_GDT
GdtPtr dw GdtLen - 1
    dd 0        ; gdt 基址

; gdt selector 
SelectorCode32  equ PM_DESC_CODE32  - PM_GDT
SelectorData32  equ PM_DESC_DATA32  - PM_GDT
SelectorStack32 equ PM_DESC_STACK32 - PM_GDT
SelectorTest    equ PM_DESC_TEST    - PM_GDT
SelectorVideo   equ PM_DESC_VIDEO   - PM_GDT
; end of [SECTION .gdt]


[SECTION .data1]
align 32    
[BITS 32]
PM_DATA:
PMMessage db "Potect Mode", 0
OffsetPMMessage equ PMMessage - $$
DATALen equ $ - PM_DATA



; 全局stack
[SECTION .gs]
ALIGN 32
[BITS 32]
PM_STACK:
    times 512 db 0
TopOfStack equ $ - PM_STACK - 1
; End of stack  
 




[SECTION .s32]
[BITS 32]
PM_SEG_CODE32:
    mov ax, SelectorVideo
    mov gs, ax

    mov byte [gs:0x00], '5'
    mov byte [gs:0x01], 0xA4
    jmp $

    mov ax, SelectorData32      ; 把数据段选择子加载到段寄存器中
    mov ds, ax

    mov ax, SelectorTest       ; 把数据段选择子加载到段寄存器中
    mov es, ax

    mov ax, SelectorVideo       ; 把数据段选择子加载到段寄存器中
    mov gs, ax

    mov ax, SelectorStack32       ; 把数据段选择子加载到段寄存器中
    mov ss, ax
    mov esp, TopOfStack


    mov ax, 0xb800
    mov gs, ax
    mov byte [gs:0x00], '4'
    mov byte [gs:0x01], 0xA4
    jmp$ 

    mov ah, 0cH
    xor esi, esi
    xor edi, edi
    mov esi, OffsetPMMessage
    mov edi, (80 * 10 + 0) * 2
    cld

.1:
    lodsb
    test al, al
    jz .2
    mov [gs:edi], ax
    add edi, 2
    jmp .1

.2:     ; 显示完毕
    jmp $
    
SegCode32Len equ $ - PM_SEG_CODE32