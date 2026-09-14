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

        mov eax, NormalHandler - PM_SEG_CODE32
        mov word [PM_IDT + 0x20 * 8], ax
        shr eax, 16
        mov word [PM_IDT + 0x20 * 8 +6], ax

        ; load IDTR
        xor eax, eax
        mov ax, ds
        shl eax, 4
        add eax, PM_IDT
        mov dword [IdtPtr + 2], eax
        sidt [_SavedIDTR]  ; 保存原来的IDTR

        in al, 21h
        mov [_SavedIMREG], al  ; 保存原来的中断屏蔽寄

        lgdt [GdtPtr] 
        lidt [IdtPtr]  ; 加载新的IDTR


        ; A20 
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

    ; IDTR
    _SavedIDTR: dd 0
                dd 0
    _SavedIMREG: dd 0       ; 中断屏蔽
    ; 保护模式
    SavedIDTR equ _SavedIDTR - $$
    SavedIMREG equ _SavedIMREG - $$
    ; end of [SECTION .data1]




    ;IDT
    [SECTION .idt]
    ALIGN 32
    [BITS 32]
    PM_IDT:
    %rep 128
        dw 0
        dw SelectorCode32
        dw 0x8E00
        dw 0
    %endrep
    IdtLen equ $ - PM_IDT
    IdtPtr  dw IdtLen - 1
            dd 0        ; idt 基址


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


        xor esi, esi
        xor edi, edi
        mov esi, OffsetPMMessage
        mov edi, (80 * 10 + 0) * 2
        cld

        mov ax, SelectorVideo
        mov gs, ax
        mov byte [gs:0x00], '6'
        mov byte [gs:0x01], 0xA4

        mov ah, 0cH                 ; 必须放在 mov ax, SelectorVideo 之后，否则 AH 被清零
        call Init8259A
        jmp $




    ; Init8259A --------------------------------------------------------------------
    Init8259A:
        mov al, 011h
        out 020h, al        ; 主8259，ICW1。
        call    io_delay

        out 0A0h, al        ; 从8259，ICW1。
        call    io_delay

        mov al, 020h        ; IRQ0 对应中断向量 0x20
        out 021h, al        ; 主8259，ICW2。
        call    io_delay

        mov al, 028h        ; IRQ8 对应中断向量 0x28
        out 0A1h, al        ; 从8259，ICW2。
        call    io_delay

        mov al, 004h        ; IR2 对应从8259
        out 021h, al        ; 主8259，ICW3。
        call    io_delay

        mov al, 002h        ; 对应主8259的 IR2
        out 0A1h, al        ; 从8259，ICW3。
        call    io_delay

        mov al, 001h
        out 021h, al        ; 主8259，ICW4。
        call    io_delay

        out 0A1h, al        ; 从8259，ICW4。
        call    io_delay

        mov al, 11111110b   ; 仅仅开启定时器中断
        ;mov al, 11111111b   ; 屏蔽主8259所有中断
        out 021h, al        ; 主8259，OCW1。
        call    io_delay

        mov al, 11111111b   ; 屏蔽从8259所有中断
        out 0A1h, al        ; 从8259，OCW1。
        call    io_delay

        ret

    ; Init8259A --------------------------------------------------------------------

    io_delay:
        nop
        nop
        nop
        nop
        ret

    ;
    NormalHandler:
        mov ah, 0cH
        mov al, 'X'
        mov [gs:((80 * 10 + 0) * 2)], ax
        jmp $
        iretd
    ; int handler -------------------------------------------------------------------
    SegCode32Len equ $ - PM_SEG_CODE32   ; 必须放在整个 32 位段（含 Init8259A/NormalHandler）之后
