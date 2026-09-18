LOADER_BASE_ADDR    equ 0x900


KERNAL_BASE_ADDR    equ 0x08000     ;kernal起始
KERNAL_BASE_SECTOR  equ 9           ; 从第9扇区开始加载内核
KERNAL_SECTOR_COUNT equ 5  


org LOADER_BASE_ADDR
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov sp, 0x7c00
    mov ax, 0xb800
    mov gs, ax
    mov eax, KERNAL_BASE_SECTOR
    mov bx, KERNAL_BASE_ADDR
    mov cx, KERNAL_SECTOR_COUNT
    call read_sectors
    jmp KERNAL_BASE_ADDR

read_sectors:
    mov esi, eax
    mov di, cx
    
    mov dx, 0x1f2
    mov al, cl
    out dx, al
    mov eax, esi

    mov cl, 8

    mov dx, 0x1f3
    out dx, al

    shr eax, cl
    mov dx, 0x1f4
    out dx, al

    shr eax, cl
    mov dx, 0x1f5
    out dx, al

    shr eax, cl
    and al, 0x0f
    or al, 0xe0     ; 设置7-4位为1110
    mov dx, 0x1f6
    out dx, al

    ; 写读命令
    mov dx, 0x1f7
    mov al, 0x20
    out dx, al

    .not_ready:
        nop
        in al, dx
        and al, 0x88
        cmp al, 0x08
        jnz .not_ready
    
    ; 读数据
    mov ax, di
    mov dx, 256
    mul dx
    mov cx, ax
    mov dx, 0x1f0

    .go_on:
        push dx
        mov dx, 0x1f7
    .wait_drq:
        in al, dx
        and al, 0x88
        cmp al, 0x08
        jnz .wait_drq
        pop dx
        in ax, dx
        mov [bx], ax
        add bx, 2
        loop .go_on
    
    ret


