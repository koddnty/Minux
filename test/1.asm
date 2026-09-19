[section .data]
strHello db 'Hello, World!', 0Ah
strlen equ $ - strHello

[section .text]
global _start
_start:
    mov edx, strlen
    mov ecx, strHello
    mov ebx, 1          ; file descriptor 1 is stdout
    mov eax, 4          ; syscall number for sys_write
    int 0x80            ; call kernel

    mov ebx, 0          ; exit code 0
    mov eax, 1          ; syscall number for sys_exit
    int 0x80            ; call kernel