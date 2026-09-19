extern chooseNum

[section .data]
    num1st db 5
    num2nd db 10

[section .text]
global _start
global mPrint
_start:
    push num2nd
    push num1st
    call chooseNum
    add esp, 8
    mov ebx, 0          ; exit code 0
    mov eax, 1          ; syscall number for sys_exit
    int 0x80            ; call kernel



mPrint:
    mov edx, [esp + 8]   ; length of the string
    mov ecx, [esp + 4]   ; pointer to the string
    mov ebx, 1           ; file descriptor 1 is stdout
    mov eax, 4           ; syscall number for sys_write
    int 0x80             ; call kernel  
    ret
