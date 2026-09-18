[BITS 16]
[section .text]
global _start
_start:
    mov ax, 0xB800
    mov es, ax
    
    mov byte [es:0x00], 'L'
    mov byte [es:0x01], 0x07

.halt:                  ; 内核必须有结尾死循环，否则会掉进后面的数据里继续执行
    hlt
    jmp .halt
