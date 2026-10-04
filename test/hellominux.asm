; hellominux —— 最小用户程序（ring3）
;   往显存写一行字, 然后调 exit 系统调用。
; 由 test/Makefile 编成【裸二进制】, 再由 host/test_build.c 装进 minux 的 /test/hellominux,
; 内核用 miCoreProgramLoader() 把它搬到 0x400000 跑起来。
[BITS 32]

[section .text]
global _start

_start:
    ; ring3 里数据段必须换成用户段, 选择子要带 RPL=3(0x23)
    mov ax, 0x23
    mov ds, ax
    mov es, ax

    ; 往显存第 10 行写 "Hello, Minux!"
    mov esi, msg
    mov edi, 0xB8000 + 160 * 10
    mov ah, 0x07                    ; 黑底白字
.next:
    lodsb                           ; AL <- DS:ESI, ESI++
    test al, al
    jz .done
    mov [edi], ax                   ; 字符 + 属性
    add edi, 2
    jmp .next

.done:
    xor ebx, ebx                    ; status = 0
    mov eax, 1                      ; 系统调用号 1 = exit
    int 0x80

.hang:
    jmp .hang                       ; exit 不应该返回, 保险

[section .rodata]
msg db 'Hello, Minux!', 0
