DA_32 EQU 4000h         ; 32位代码段属性
DA_C EQU 98h            ; 只执行代码段属性
DA_DRW EQU 92h          ; 可读写数据段属性
DA_DRWA EQU 93h          ; 存在的已访问的可读写数据段属性
DA_LDT EQU 82h          ; LDT段属性


DA_DPL_0 EQU 00h         ; DPL=0
DA_DPL_1 EQU 20h         ; DPL=1
DA_DPL_2 EQU 40h         ; DPL=2
DA_DPL_3 EQU 60h         ; DPL=3

SA_TIL EQU 4h           ; LDT选择子中的TI位

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


    ; 初始化32位LDT
    xor eax, eax
    mov ax, cs
    shl eax, 4
    add eax, LABEL_LDT
    mov word [LABEL_DESC_LDT + 2], ax
    shl eax, 16
    mov byte [LABEL_DESC_LDT + 4], al
    mov byte [LABEL_DESC_LDT + 7], ah
    ; LDT
    xor eax, eax
    mov ax, cs
    shl eax, 4
    add eax, LABEL_CODEA
    mov word [LABEL_LDT_DESC_CODEA + 2], ax
    shl eax, 16
    mov byte [LABEL_LDT_DESC_CODEA + 4], al
    mov byte [LABEL_LDT_DESC_CODEA + 7], ah


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
    jmp dword SelectorCode32:0
    



[SECTION .gdt]              ;   段基址      界限                属性
PM_GDT:             Descriptor  0,          0,                  0
PM_DESC_CODE32:     Descriptor  0,          SegCode32Len - 1,   DA_32 | DA_C
PM_DESC_DATA32:     Descriptor  0,          DATALen - 1,        DA_DRW
PM_DESC_STACK32:    Descriptor  0,          TopOfStack - 1,     DA_DRW + DA_32
PM_DESC_TEST:       Descriptor  0200000h,   0ffffh,             DA_DRW
PM_DESC_VIDEO:      Descriptor  0B8000h,    0ffffh,             DA_DRW

LABEL_DESC_LDT:     Descriptor  0,          LDTLen - 1,         DA_LDT
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

SelectorLDT     equ LABEL_DESC_LDT  - PM_GDT
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


    mov ax, SelectorData32      ; 把数据段选择子加载到段寄存器中
    mov ds, ax

    mov ax, SelectorTest       ; 把数据段选择子加载到段寄存器中
    mov es, ax

    mov ax, SelectorVideo       ; 把数据段选择子加载到段寄存器中
    mov gs, ax

    mov ax, SelectorStack32       ; 把数据段选择子加载到段寄存器中
    mov ss, ax
    mov esp, TopOfStack



    ; 打印6
    mov ax, SelectorVideo
    mov gs, ax
    mov byte [gs:0x00], '6'
    mov byte [gs:0x01], 0xA4


    xor esi, esi
    xor edi, edi
    mov esi, OffsetPMMessage
    mov edi, (80 * 10 + 0) * 2
    cld
    mov ah, 0cH

.1:
    lodsb
    test al, al
    jz .2
    mov [gs:edi], ax
    add edi, 2
    jmp .1

.2:     ; 显示完毕
    mov ax, SelectorLDT
    lldt ax 
    jmp SelectorLDTCodeA:0
    
SegCode32Len equ $ - PM_SEG_CODE32







; LDT
[SECTION .ldt]
ALIGN 32
LABEL_LDT:
    LABEL_LDT_DESC_CODEA: Descriptor        0, CodeALen - 1, DA_C + DA_32

LDTLen equ $ - LABEL_LDT

; 选择子
SelectorLDTCodeA equ LABEL_LDT_DESC_CODEA - LABEL_LDT + SA_TIL
; end of ldt


[SECTION .la]
ALIGN 32
[BITS 32]
LABEL_CODEA:
    mov ax, SelectorVideo
    mov gs, ax
    mov byte [gs:0x00], 'Y'
    mov byte [gs:0x01], 0Ch
    jmp $
CodeALen equ $ - LABEL_CODEA
; end of codeA
