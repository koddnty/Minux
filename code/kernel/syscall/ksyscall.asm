global syscall_entry
global syscall_return

; 选择子在 kernel_entry.asm 里定义(GDT 是它的), 用 extern 引过来
extern SelectorKernelData32
extern kernel_write
extern miCoreFork
extern miCoreExec
extern miCoreGetPid

;EBX = arg1
;ECX = arg2
;EDX = arg3
;ESI = arg4
;EDI = arg5


[SECTION .text]
[BITS 32]


; 系统调用入口
syscall_entry:
    ; 上下文数据压栈
    push ds
    push es
    push fs
    push gs
    pushad
    ; 段切换
    mov ax, SelectorKernelData32
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    ; 进入系统调用

    ; 用 pushad 里保存的原始 eax: 压栈顺序是 EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI,
    ; eax 在最高地址 = [esp + 28]。用【间接跳转】连 edx(参数3) 都不碰。
    cmp dword [esp + 28], SYSCALL_COUNT
    jae syscall_bad                 ; 未知调用号: 返回 -1, 不跳飞

    mov eax, [esp + 28]             ; 取回调用号(eax 不是参数, 改它没关系)
    jmp dword [syscall_table + eax * 4]

syscall_bad:
    ; repair: 往 pushad 帧里的 eax 位置写 -1, popad 时就还给调用方了
    mov dword [esp + 28], -1

syscall_return:
    ; 上下文恢复
    popad
    pop gs
    pop fs
    pop es
    pop ds
    iretd


; 系统调用入口汇编 ----------------------------------------
; kernel_write 系统调用
kernel_write_entry:
    push ebx
    call kernel_write
    add esp, 4
    jmp syscall_return


; 系统调用表 ----------------------------------------
[section .data]
; repair: 表项个数, syscall_entry 用它做越界检查(加新调用别忘了改这个数)
SYSCALL_COUNT equ 5

syscall_table:
    dd syscall_exit
    dd kernel_write_entry
    dd syscall_fork                         ; 进程创建
    dd syscall_exec                         ; 加载并执行
    dd syscall_get_pid                      ; 获取当前 pid


; fork: 把当前 syscall 现场指针传给内核, 返回值写回保存的 EAX
[section .text]
syscall_fork:
    push esp
    call miCoreFork
    add esp, 4
    mov [esp + 28], eax
    jmp syscall_return


; exec: EBX 是用户传进来的路径指针
syscall_exec:
    push ebx
    call miCoreExec
    add esp, 4
    mov [esp + 28], eax
    jmp syscall_return


; get_pid: 没有参数
syscall_get_pid:
    call miCoreGetPid
    mov [esp + 28], eax
    jmp syscall_return


; 其他 ----------------------------------------
syscall_exit:
.exit:
    cli
    hlt
    jmp .exit

