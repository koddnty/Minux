global syscall_entry

; 选择子在 kernel_entry.asm 里定义(GDT 是它的), 用 extern 引过来
extern SelectorKernelData32

[SECTION .text]
[BITS 32]

extern kVagprintf

; 系统调用入口
syscall_entry:
    push ds
    push es
    push fs
    push gs

    pushad

    mov ecx, eax

    mov ax, SelectorKernelData32
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax


    mov edx, [syscall_table + eax * 4]
    jmp edx

    popad

    pop gs
    pop fs
    pop es
    pop ds

    iretd


; 系统调用表
[section .data]
syscall_table:
    dd syscall_exit
    dd kVagprintf



syscall_exit:
    ; repair: 用户程序调 exit 走到这里。往显存第 12 行写 "OK", 用来证明
    ; repair: 控制权确实从 ring3 回到了 ring0(这一段是内核在跑, 用内核态地址)。
    mov byte [0xB8000 + 160 * 12],     'O'
    mov byte [0xB8000 + 160 * 12 + 1], 0x0A
    mov byte [0xB8000 + 160 * 12 + 2], 'K'
    mov byte [0xB8000 + 160 * 12 + 3], 0x0A
.exit:
    cli
    hlt
    jmp .exit

