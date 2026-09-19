#! /bin/bash
# 用 QEMU + GDB 调试 MBR / loader / 内核
#
#   ./debug.sh                 # 用 ../../nvme0virtual，启动后停在第一条指令，等 gdb
#   ./debug.sh disk.img        # 用别的镜像（建议用副本，别污染正在用的盘）
#   ./debug.sh disk.img -x     # 顺便自动拉起 gdb -x debug.gdb
#   QEMU_EXTRA="-display none" ./debug.sh disk.img   # 无窗口（纯终端/远程时用）
#
# 日志：QEMU 的异常/TB 追踪写在 qemu.log（-d int,cpu_reset,in_asm）
set -e

IMAGE="${1:-../../nvme0virtual}"
[ $# -gt 0 ] && shift
AUTO_GDB=0
[ "$1" = "-x" ] && AUTO_GDB=1

KERNAL_SECTOR=9               # 要和 loader.asm 的 KERNAL_BASE_SECTOR 一致
KERNAL_SECTORS=5              # 要和 loader.asm 的 KERNAL_SECTOR_COUNT 一致

# 1) 构建
nasm -f bin MBR.asm    -o ../bin/MBR.bin
nasm -f bin loader.asm -o ../bin/loader.bin
nasm -f elf kernal.elf.asm -o kerna.elf.nasm.o
ld -m elf_i386 kerna.elf.nasm.o -Ttext 0x08000 -o kernal.dbg.elf   # 不加 -s：保留符号给 gdb
objcopy -O binary kernal.dbg.elf kraw.bin
truncate -s $((KERNAL_SECTORS * 512)) kraw.bin                     # 补齐扇区，冲掉旧残留

# 2) 写盘
dd if=../bin/MBR.bin    of="$IMAGE" bs=512 count=1             conv=notrunc
dd if=../bin/loader.bin of="$IMAGE" bs=512 seek=2              conv=notrunc
dd if=kraw.bin          of="$IMAGE" bs=512 seek=$KERNAL_SECTOR conv=notrunc

# 3) 起 QEMU：-s 开 gdbstub(1234)，-S 冻在开机第一条指令
echo "QEMU 已启动（gdbstub: localhost:1234，CPU 暂停中），日志 qemu.log"
qemu-system-i386 -drive file="$IMAGE",format=raw \
    -s -S -no-reboot -no-shutdown $QEMU_EXTRA \
    -d int,cpu_reset,in_asm -D qemu.log &
QPID=$!
echo "qemu pid=$QPID    （结束调试：kill $QPID）"
sleep 1

if [ "$AUTO_GDB" = "1" ]; then
    gdb -q -x debug.gdb
fi
