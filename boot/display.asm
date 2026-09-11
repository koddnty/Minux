org 07c00h
mov ax, 0xB800     ; 指向文本模式显示缓冲区
mov es, ax
mov byte [es:0x00], '9'
mov byte [es:0x01], 0x47

times 510 - ($-$$) db 0
dw 0xaa55