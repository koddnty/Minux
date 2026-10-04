; hellominux —— 测试程序入口
;   本文件只干"进用户态之后把环境摆好, 然后 call main",
;   真正的测试逻辑在 hellominux.c 里。
;
; 注意: miCoreProgramLoader 把整个文件【原样】拷到 0x400000 并从偏移 0 开始执行,
;       所以 _start 必须落在链接后地址 0x400000 —— test.ld 的 `. = 0x400000` 保证这点。
[BITS 32]

[section .text]
global _start
extern main

_start:
    ; ring3 里数据段必须换成用户段, 选择子要带 RPL=3(0x23)
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    and esp, 0xFFFFFFF0             ; 16 字节对齐, 免得 C 代码用上 SSE 指令时炸
    call main                       ; 跑测试逻辑

    ; main 返回了 -> 按 exit(0) 处理
    xor ebx, ebx                    ; status = 0
    xor eax, eax                    ; 系统调用号 0 = exit
    int 0x80

.hang:
    jmp .hang                       ; exit 不应该返回, 保险
