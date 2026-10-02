# ============================================================================
#  Minux 构建入口 —— 一条命令从源码到可引导镜像
#
#    make            编译全部 + 组装磁盘镜像（默认目标）
#    make run        组装镜像后启动 QEMU
#    make debug      组装镜像后带异常日志启动 QEMU（日志写 qemu.log）
#    make tools      只编宿主机侧工具（host/*.c）
#    make fs         只把宿主机文件系统工具跑一遍（对着镜像）
#    make clean      删掉编译产物
#    make help       打印这段说明
#
#  常用覆盖：make IMAGE=../other.img  /  make QEMU=qemu-system-i386
# ============================================================================

IMAGE          ?= ../nvme0virtual
QEMU           ?= qemu-system-x86_64
SHELL          := /bin/bash     # check 目标里用了 $((16#..))，dash 不认，必须 bash

BIN            := bin

# 磁盘布局：只在这里写一次 ----------------------------------------
#   loader.asm 里的 LOADER_START_SECTOR / KERNEL_SECTOR_BEGIN / KERNEL_SECTOR_COUNT
#   必须和下面这几个值一致，make check 会替你核对。
SECTOR_MBR     := 0
SECTOR_LOADER  := 3
SECTOR_KERNEL  := 32
KERNEL_SECTORS := 16
KERNEL_BASE    := 0x10000
IMAGE_MB       := 128

NASM      := nasm
CC        := gcc
NASMFLAGS := -f bin

# 宿主机侧（有 glibc）
CFLAGS    := -O2 -Wall -D_FILE_OFFSET_BITS=64 -I code -I host

# 内核源码：递归收 code/kernel 下所有 .c / .asm
#   子目录（lib/、syscall/ …）自动包含 —— 想加新模块就在 code/kernel/ 下建个子目录，
#   比如把系统调用放 code/kernel/syscall/{syscall.c,syscall.h}，Makefile 不用改。
KERNEL_C    := $(shell find code/kernel -name '*.c')
KERNEL_ASM  := $(shell find code/kernel -name '*.asm')

# 每个内核子目录都进 include 路径，代码里可以直接 #include "kprintf.h"
# repair: 原来写的是 $(dir $(shell find … -type d))，而 $(dir) 是"取文件所在目录"的，
# repair: 对目录用它会把最后一段吃掉（code/kernel/lib → code/kernel/），子目录其实没进搜索路径。
KERNEL_INC  := $(sort $(addsuffix /,$(shell find code/kernel -type d)))

KERNEL_OBJS := $(patsubst code/kernel/%.asm,$(BIN)/kernel/%.asm.o,$(KERNEL_ASM)) \
               $(patsubst code/kernel/%.c,$(BIN)/kernel/%.c.o,$(KERNEL_C))

# 内核侧（freestanding，不能用 glibc；<string.h> 取自 code/tools）
KCFLAGS   := -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin \
             -fno-asynchronous-unwind-tables -I code -I code/tools \
             $(addprefix -I ,$(KERNEL_INC))

HOST_SUPPORT := host/fsSys.c                    # 公共实现（没有 main，不单独编成工具）
HOST_SRCS  := $(filter-out $(HOST_SUPPORT),$(wildcard host/*.c))
HOST_TOOLS := $(patsubst host/%.c,$(BIN)/%,$(HOST_SRCS))

.PHONY: all image run debug tools fs check clean help kernel-src

all: image

# ---------------------------------------------------------------------------
# 引导链
# ---------------------------------------------------------------------------
$(BIN)/MBR.bin: code/boot/MBR.asm | $(BIN)
	$(NASM) $(NASMFLAGS) $< -o $@

$(BIN)/loader.bin: code/boot/loader.asm | $(BIN)
	$(NASM) $(NASMFLAGS) $< -o $@


# ---------------------------------------------------------------------------
# 内核：code/kernel/**/*.asm + **/*.c  →  elf  →  裸二进制 kernel.bin
#
#   和你编用户程序（gcc -m32 -c / ld -m elf_i386）是同一套路，区别只有三点：
#     1) 加 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin（没有 libc）
#     2) 链接用内核自己的脚本 kernel.ld（决定加载地址 0x10000 和 __bss_* 符号）
#     3) 最后 objcopy -O binary 出裸二进制，由 loader 直接读到内存里跑
#
#   目标文件按源码目录结构放到 bin/kernel/ 下（如 bin/kernel/lib/kprintf.c.o）
# ---------------------------------------------------------------------------
$(BIN)/kernel/%.asm.o: code/kernel/%.asm
	@mkdir -p $(dir $@)
	$(NASM) -f elf32 $< -o $@

$(BIN)/kernel/%.c.o: code/kernel/%.c
	@mkdir -p $(dir $@)
	$(CC) $(KCFLAGS) -c $< -o $@

$(BIN)/kernel.bin: $(KERNEL_OBJS) code/kernel/kernel.ld | $(BIN)
	ld -m elf_i386 -T code/kernel/kernel.ld -o $(BIN)/kernel.elf $(KERNEL_OBJS)
	objcopy -O binary $(BIN)/kernel.elf $@

# 列出内核实际会编哪些文件（排查"文件没被编进去"很有用）
kernel-src:
	@echo "内核 C   源文件:"; for f in $(KERNEL_C);   do echo "  $$f"; done
	@echo "内核 asm 源文件:"; for f in $(KERNEL_ASM); do echo "  $$f"; done
	@echo "include 路径:";    for f in $(KERNEL_INC); do echo "  $$f"; done

# 用户程序（test/ 里那套）：交给 FS 装盘后由内核加载
#   参考命令就是我以前手动敲的那两条：
#     nasm -f elf32 xx.asm -o xx.o
#     gcc -m32 -c yy.c -o yy.o
#     ld -m elf_i386 -s -o prog xx.o yy.o

# ---------------------------------------------------------------------------
# 宿主机侧工具（文件系统读写、未来的 mkfs 都放 host/）
#   注意：宿主编译用 glibc 的 string.h，不要加 -I code/tools（那个是给内核用的）
# ---------------------------------------------------------------------------
tools: $(HOST_TOOLS)

# 规则：host/xxx.c  →  bin/xxx（自动带上 fsTree.c 和 fsSys.c）
$(BIN)/%: host/%.c code/minFs/fsTree.c $(HOST_SUPPORT) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^

fs: $(BIN)/build
	$(BIN)/build $(IMAGE)

$(BIN):
	@mkdir -p $(BIN)

# 组装镜像 --------------------------------------------------
# repair: $(BIN)/build 必须写进依赖里。原来只写在 recipe 里，会有两个问题：
# repair:   * make clean 之后 bin/build 不存在 → 这一行报 "没有那个文件或目录"（错误 127）
# repair:   * 改了 host/build.c 也不会重编，跑的还是旧工具（和之前 'A'/'B' 同一类问题）
image:
	@echo "== 清零镜像 $(IMAGE) ($(IMAGE_MB)MB) =="
	@dd if=/dev/zero of=$(IMAGE) bs=1M count=$(IMAGE_MB) status=none
	@dd if=$(BIN)/MBR.bin    of=$(IMAGE) bs=512 seek=$(SECTOR_MBR)    count=1 conv=notrunc status=none
	@dd if=$(BIN)/loader.bin of=$(IMAGE) bs=512 seek=$(SECTOR_LOADER) conv=notrunc status=none
	@dd if=$(BIN)/kernel.bin of=$(IMAGE) bs=512 seek=$(SECTOR_KERNEL) conv=notrunc status=none
	@echo "镜像组装完成：MBR@$(SECTOR_MBR)、loader@$(SECTOR_LOADER)、kernel@$(SECTOR_KERNEL)"
	$(BIN)/build $(IMAGE)            # ← 最后装文件系统；加 $(BIN)/build 作为依赖，改 host/*.c 会自动重编
	@echo "文件系统构建完成"

run: image
	$(QEMU) -drive file=$(IMAGE),format=raw

debug: image
	@echo "== 启动 QEMU，异常日志写 qemu.log（Ctrl-C 结束）=="
	$(QEMU) -drive file=$(IMAGE),format=raw -no-reboot -no-shutdown \
	    -d int,cpu_reset -D qemu.log
	@echo "挂了就看：grep -nE 'v=0[0-9a-f]{2}|Triple fault' qemu.log | tail"

clean:
	rm -f $(BIN)/MBR.bin $(BIN)/loader.bin $(BIN)/loader.o $(BIN)/loader.elf \
	      $(BIN)/kernel.bin $(BIN)/kernel.elf $(HOST_TOOLS) qemu.log
	rm -rf $(BIN)/kernel

# ---------------------------------------------------------------------------
# 布局自检：Makefile 和 asm 里的三组常量必须对得上（以前踩过好几次）
#   不想要它就把 image 依赖里的 check 去掉，改成手动 `make check`。
# ---------------------------------------------------------------------------

help:
	@sed -n '2,14p' Makefile | sed 's/^# \{0,1\}//'
	@echo ""
	@echo "当前布局：镜像=$(IMAGE)"
	@echo "  MBR@$(SECTOR_MBR)  loader@$(SECTOR_LOADER)  kernel@$(SECTOR_KERNEL)（$(KERNEL_SECTORS) 扇区，加载到 $(KERNEL_BASE)）"
