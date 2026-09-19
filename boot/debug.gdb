# GDB 调试脚本：配合 debug.sh 启动的 QEMU（-s -S）使用
#   gdb -q -x debug.gdb
# 注意：GDB 命令文件里行尾不能写 # 注释，注释要单独一行
set confirm off
set pagination off
set disassembly-flavor intel

target remote :1234

# 内核符号：kernal.dbg.elf 是没加 -s 的 ELF（链接在 0x8000）
add-symbol-file kernal.dbg.elf 0x8000

# 三个阶段入口
break *0x7c00
break *0x900
break _start

define screen
  monitor xp /32xb 0xb8000
end

define regs
  monitor info registers
end

echo \n=== 快捷命令 ===\n
echo   c              继续跑（会依次停在 MBR / loader / 内核）\n
echo   si, ni         单步：进 / 不进 call\n
echo   regs           段基址 + GDT/IDT 基址（走 QEMU monitor）\n
echo   screen         看显存 0xb8000 前 32 字节（每 2 字节 = 字符+属性）\n
echo   x/8i \$pc       反汇编（注意 GDB 按 64 位反汇编，16 位代码请用 ndisasm 看）\n
echo   watch *(unsigned char*)0xb8000     谁在改屏幕\n
echo   monitor xp /64xb 0x8000            看物理内存\n
echo   monitor screendump shot.ppm        截屏\n
echo =================\n
