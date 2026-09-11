#!/bin/bash
# 用法: ./flash.sh display.asm
SRC=${1:?用法: $0 <源文件.asm> [镜像]}
IMG=${2:-../../nvme0virtual}

nasm "$SRC" -o "../bin/${SRC%.asm}.bin" || exit 1
dd if="$IMG" of="$IMG.backup" bs=512 count=1 conv=notrunc status=none
dd if="../bin/${SRC%.asm}.bin" of="$IMG" bs=512 count=1 conv=notrunc status=none
echo "已刷入 ${SRC%.asm}.bin -> $IMG，备份在 $IMG.backup"
