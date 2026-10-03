global syscall_entry

; 选择子在 kernel_entry.asm 里定义(GDT 是它的), 用 extern 引过来
extern SelectorKernelData32

[SECTION .text]
[BITS 32]

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

    cmp ecx, 1
    je syscall_exit

    popad

    pop gs
    pop fs
    pop es
    pop ds

    iretd


syscall_exit:
.exit:
    cli
    hlt
    jmp .exit

