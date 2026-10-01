BITS 16
[global printInPos]
[global printChar]        ; 输出单个字符
[global getKeyboardInput]          ; 获得键盘输入

getKeyboardInput:               ; 获取键盘字符放到tempc中
    mov ah, 0
    int 16h
    mov ah, 0
    retf

printChar:                     ; 输出单个字符
    pusha
    mov bp, sp
    add bp, 16 + 4
    mov al, [bp]
    mov bh, 0
    mov ah, 0Eh
    int 10h
    popa
    retf

printInPos:                    ; 在指定位置输出字符串
    pusha
    mov si, sp
    add si, 16 + 4
    mov ax, cs
    mov ds, ax
    mov bp, [si]          ; 获取行号
    mov ax, ds
    mov es, ax

    mov cx, [si + 4]      ; 获取列号
    mov ax, 1301h
    mov bx, 0007h
    mov dh, [si + 8]
    mov dl, [si + 12]
    int 10h
    popa
    retf
    
