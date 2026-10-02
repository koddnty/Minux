[BITS 32]

global miReadSector
global miWriteSector


%ifndef MI_READSECTOR_NO_SECTION
section .text
%endif


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
    mov dx, 0x1F7               ;
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
