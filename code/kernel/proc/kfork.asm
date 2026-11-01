[section .code32]
[bits 32]

global mi_core_switch_process_to    ; 进程切换

mi_core_switch_process_to:
    ; 调用约定:
    ;   [esp+4]  = current_context_esp 指针
    ;   [esp+8]  = next_context_esp
    ;   [esp+12] = next_page_directory
    ;
    ; pushfd + pushad 之后, 栈布局:
    ;   [esp+0]  EDI ... [esp+28] EAX
    ;   [esp+32] EFLAGS
    ;   [esp+36] 返回地址
    ;   [esp+40] arg1
    ;   [esp+44] arg2
    ;   [esp+48] arg3

    ; 保存当前进程上下文, 切换期间关中断
    pushfd
    cli
    pushad

    ; 保存当前进程内核栈 ESP 到 *current_context_esp
    mov eax, [esp + 40]
    mov [eax], esp

    ; 获取目标进程页目录物理地址, 先切 CR3
    mov eax, [esp + 48]
    mov cr3, eax

    ; 切换到目标进程内核栈
    mov esp, [esp + 44]

    ; 恢复目标进程通用寄存器和 EFLAGS
    popad
    popfd

    ; 返回目标进程之前保存的位置
    ret