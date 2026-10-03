[section .code32]
[bits 32]

global miCoreGetCR2         ;   发生缺页的虚拟地址
global miCoreGetCR3         ;   当前PageDirectory
global miCoreLoadCR3        ;   切换当前地址空间
global miCoreInvalidatePage     ; tlb缓存丢弃

miCoreGetCR2:
    mov eax, cr2
    ret

miCoreGetCR3:
    mov eax, cr3
    ret

miCoreLoadCR3:
    mov eax, [esp + 4]
    mov cr3, eax
    ret

miCoreInvalidatePage:
    mov eax, [esp + 4]
    invlpg [eax]
    ret