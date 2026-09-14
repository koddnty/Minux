#! /bin/bash



nasm -f bin MBR.asm -o ../bin/MBR.bin
nasm -f bin idt_test.asm -o ../bin/ps.bin

dd if=../bin/MBR.bin of=../../nvme0virtual bs=512 count=1 conv=notrunc
dd if=../bin/ps.bin of=../../nvme0virtual bs=512 seek=2 conv=notrunc

qemu-system-x86_64 -drive file=../../nvme0virtual,format=raw
