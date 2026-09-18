#!/bin/bash

set -e

BasePath=/home/koddnty/user/projects/minux

NVME_PATH="$BasePath/nvme0virtual"
BIN_DIR="$BasePath/minux/bin"
BOOT_DIR="$BasePath/minux/boot"

# 清空旧内容
dd if=/dev/zero of="$NVME_PATH" bs=1M count=128

# 编译
nasm -f bin "$BOOT_DIR/MBR.asm"    -o "$BIN_DIR/MBR.bin"
nasm -f bin "$BOOT_DIR/loader.asm" -o "$BIN_DIR/loader.bin"
nasm -f bin "$BOOT_DIR/ps.asm"     -o "$BIN_DIR/ps.bin"

# 写入镜像
dd if="$BIN_DIR/MBR.bin"    of="$NVME_PATH" bs=512 count=1 conv=notrunc
dd if="$BIN_DIR/loader.bin" of="$NVME_PATH" bs=512 seek=2  conv=notrunc
dd if="$BIN_DIR/kraw.bin"   of="$NVME_PATH" bs=512 seek=9  conv=notrunc

# 启动
qemu-system-x86_64 -drive file="$NVME_PATH",format=raw