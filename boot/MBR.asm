LOADER_BASE_ADDR    equ 0x900
LOADER_START_SECTOR equ 02h
LOADER_SECTOR_COUNT equ 02h    ; 必须 >= ceil(ps.bin 大小 / 512)，目前 ps.bin = 2123B

org 0x7c00
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov sp, 0x7c00
    mov ax, 0xb800
    mov gs, ax

    mov ah, 0x06
    mov al, 0x00
    mov bh, 0x17
    mov cx, 0x0000
    mov dx, 0x184F
    int 0x10

    mov byte [gs:0x00], '1'
    mov byte [gs:0x01], 0xA4

    mov byte [gs:0x02], ' '
    mov byte [gs:0x03], 0xA4

    mov byte [gs:0x04], 'M'
    mov byte [gs:0x05], 0xA4

    mov byte [gs:0x06], 'B'
    mov byte [gs:0x07], 0xA4

    mov byte [gs:0x08], 'R'
    mov byte [gs:0x09], 0xA4

    mov eax, LOADER_START_SECTOR
    mov bx, LOADER_BASE_ADDR
    mov cx, LOADER_SECTOR_COUNT
    call read_sectors   

    jmp LOADER_BASE_ADDR

;  eax = 起始扇区号（LBA）
;  bx  = 数据读到内存的地址
;  cx  = 要读几个扇区
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

times 510 - ($-$$) db 0
dw 0xaa55