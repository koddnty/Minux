[BITS 32]

global miReadSector
global miWriteSector

section .text

; ---------------------------------------------------------------------------
; 内核侧的扇区读写（ATA PIO / LBA28），和宿主机上的 host/fsSys.c 同接口：
;     int miReadSector (uint32_t lba, void*       buffer, uint32_t count)
;     int miWriteSector(uint32_t lba, const void* buffer, uint32_t count)
;   返回 0 成功，-1 失败（宿主版也是这个语义）。
;
; repair: 调用前 DS/ES 必须是「基址为 0 的平坦段」。因为 rep insw 的目地是
; repair: ES:EDI、rep outsw 的源是 DS:ESI，这两个指令用的是段基址+偏移；
; repair: 如果段基址不是 0（比如 loader 里那个基址=PM_LOADER_DATA32 的数据段），
; repair: 数据就会写到/读自错误的内存。用 loader 的 SelectorFlatData32 即可。
; ---------------------------------------------------------------------------

; int miReadSector(uint32_t lba, void* buffer, uint32_t count)
miReadSector:
    push ebp
    mov ebp, esp

    push ebx
    push esi
    push edi

    mov ebx, [ebp + 8]          ; ebx = lba
    mov edi, [ebp + 12]         ; edi = buffer
    mov esi, [ebp + 16]         ; esi = count
    ; repair: 原代码是 mov edi,[ebp+8](=lba) / mov ebx,[ebp+12](=buffer)，
    ; repair: 而 rep insw 写的是 ES:EDI —— 也就是说它把扇区数据写到了 es:lba
    ; repair: 那块内存（lba=1 时就是物理地址 1，直接踩中断向量表），
    ; repair: 调用方给的 buffer 从头到尾没被用过。现在把 buffer 放进 EDI。

    test esi, esi
    jz .read_fail               ; count == 0
    cmp esi, 255
    ja .read_fail               ; 扇区数寄存器(0x1F2)只有 8 位

    ; 扇区数 + LBA28
    mov dx, 0x1F2
    mov eax, esi
    out dx, al

    mov eax, ebx
    mov dx, 0x1F3
    out dx, al

    shr eax, 8
    mov dx, 0x1F4
    out dx, al

    shr eax, 8
    mov dx, 0x1F5
    out dx, al

    shr eax, 8
    and al, 0x0F
    or al, 0xE0

    mov dx, 0x1F6
    out dx, al

    mov dx, 0x1F7
    mov al, 0x20                ; READ SECTORS
    out dx, al

.read_sector:
.wait_ready:
    mov dx, 0x1F7               ; repair: 每读一个扇区都要重新把 DX 指回状态口。
                                ; repair: 原代码只在进入循环前设过一次，第一轮 rep insw
                                ; repair: 之后 DX 已经变成 0x1F0(数据口)，第二轮就把数据口
                                ; repair: 当状态口读 —— 多扇区读会读到错数据/卡死。
    in al, dx

    test al, 0x80               ; BSY
    jnz .wait_ready

    test al, 0x01               ; ERR
    jnz .read_fail

    test al, 0x20               ; DF
    jnz .read_fail

    test al, 0x08               ; DRQ
    jz .wait_ready

    mov dx, 0x1F0
    mov ecx, 256
    rep insw                    ; ES:EDI，EDI 自动前进 512 字节

    dec esi
    jnz .read_sector

    xor eax, eax
    jmp .read_done

.read_fail:
    mov eax, -1                 ; repair: 原来无论成功失败都返回 0，宿主版会返回 -1，
                                ; repair: 语义现在对齐了

.read_done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret


; int miWriteSector(uint32_t lba, const void* buffer, uint32_t count)
miWriteSector:
    push ebp
    mov ebp, esp

    push ebx
    push esi
    push edi

    mov ebx, [ebp + 8]          ; ebx = lba
    mov esi, [ebp + 12]         ; esi = buffer（rep outsw 的源：DS:ESI）
    mov edi, [ebp + 16]         ; edi = count
    ; repair: 原写路径用 ebx 当 buffer、esi 当源指针，还把计数放在 [ebp+16]
    ; repair: 上直接 dec（改调用方的栈参数），比较乱；统一成和读路径一样的
    ; repair: lba/源指针/计数三个寄存器。

    test edi, edi
    jz .write_fail
    cmp edi, 255
    ja .write_fail

    mov dx, 0x1F2
    mov eax, edi
    out dx, al

    mov eax, ebx
    mov dx, 0x1F3
    out dx, al

    shr eax, 8
    mov dx, 0x1F4
    out dx, al

    shr eax, 8
    mov dx, 0x1F5
    out dx, al

    shr eax, 8
    and al, 0x0F
    or al, 0xE0

    mov dx, 0x1F6
    out dx, al

    mov dx, 0x1F7
    mov al, 0x30                ; WRITE SECTORS
    out dx, al

.write_sector:
.wait_ready:
    mov dx, 0x1F7
    in al, dx

    test al, 0x80               ; BSY
    jnz .wait_ready

    test al, 0x01               ; ERR
    jnz .write_fail

    test al, 0x20               ; DF
    jnz .write_fail

    test al, 0x08               ; DRQ
    jz .wait_ready

    mov dx, 0x1F0
    mov ecx, 256
    rep outsw                   ; DS:ESI，ESI 自动前进 512 字节

    dec edi
    jnz .write_sector

    ; repair: 新增：写完等 BSY 清零再返回。否则紧接着的读/整机重启可能
    ; repair: 在最后一个扇区还没落盘时就发生，宿主那边看到的数据是旧的。
.wait_done:
    mov dx, 0x1F7
    in al, dx
    test al, 0x80
    jnz .wait_done

    xor eax, eax
    jmp .write_done

.write_fail:
    mov eax, -1

.write_done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
