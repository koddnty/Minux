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

global enter_user_mode
extern user_test


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
; repair: 错误码 bit0 = P(1=页本来就存在, 是保护性缺页), bit2 = U/S(0=内核态访问)。
; repair: 这两种都补不了页(按需分页只对 P=0 有意义), 交给 handler 只会疯狂重试成
; repair: 缺页风暴。直接停住, 用 -d int 看第一条 v=0e 的错误码就能定位。
page_fault_entry:
    mov al, [esp]
    test al, 0x01
    jnz kernel_fault_panic
    test al, 0x04
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


; 系统调用入口
syscall_entry:
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

    cmp eax, 1
    je syscall_exit

    popad

    pop gs
    pop fs
    pop es
    pop ds

    iretd


syscall_exit:
    mov byte [0xB8000], 'E'
    mov byte [0xB8001], 0x07

.exit:
    cli
    hlt
    jmp .exit


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
    ; 进 kernel C(kernel_main 里会把用户页准备好, 再调 enter_user_mode 进用户态)
    call kernel_main

    ; kernel_main 正常返回的话停在这里
kernel_hang:
    cli
    hlt
    jmp kernel_hang


; void enter_user_mode(uint32_t eip, uint32_t esp)
;   repair: 原来是 push SelectorUserData32 / SelectorUserCode32 —— 选择子没带 RPL=3,
;   repair: iret 弹 CS 时因为 CS.RPL==CPL 被当成同特权级返回: 只弹 3 项、CPL 仍是 0,
;   repair: 根本没进 ring3。必须 |3(SS=0x23, CS=0x1B)。
;   repair: EFLAGS 原来是 0x202(IF=1): PIT 的时钟中断一来, IDT 里 0x20 是空表项
;   repair: → #GP → 13 号也空 → #DF → 三重故障重启。测试阶段先给 IF=0。
global enter_user_mode
enter_user_mode:
    mov eax, [esp + 4]                  ; 用户代码入口
    mov ecx, [esp + 8]                  ; 用户栈顶
    cli

    mov dx, SelectorUserData32 | 3      ; 用户数据段(RPL=3)
    mov ds, dx
    mov es, dx
    mov fs, dx
    mov gs, dx

    push (SelectorUserData32 | 3)       ; SS     = 0x23
    push ecx                            ; ESP    = 用户栈
    push 0x2                            ; EFLAGS = IF=0(还没装时钟中断门)
    push (SelectorUserCode32 | 3)       ; CS     = 0x1B
    push eax                            ; EIP    = 用户代码
    iretd


; 用户态测试代码: 位置无关的一小段, 由 C 拷到用户页里执行
;   ring3 不能用 hlt(特权指令, 会 #GP), 所以用死循环阻塞
global user_code_start
global user_code_end
user_code_start:
    mov ax, SelectorUserData32 | 3
    mov es, ax
    mov byte [es:0xB8010], 'U'          ; 直接写显存(那一页要给 U 权限)
    mov byte [es:0xB8011], 0x07

    mov eax, 1
    int 0x80

user_code_hang:
    jmp user_code_hang
user_code_end:




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
align 8
IDT:
    ; 缺页中断
    times 14 dq 0
    dw 0                    ; offset 15:0  运行时回填
    dw SelectorKernelCode32
    db 0
    db 10001110b
    dw 0                    ; offset 31:16 运行时回填
    times 241 dq 0

        times (0x80 - 15) dq 0


    times (0x80 - 15) dq 0

    ; syscall
    dw syscall_entry & 0FFFFh
    dw SelectorKernelCode32
    db 0
    db 11101110b
    dw syscall_entry >> 16

    times (256 - 0x81) dq 0
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





