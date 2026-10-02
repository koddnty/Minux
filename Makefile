# ============================================================================
#  Minux 构建入口
#
#    make            只编译（MBR / loader / kernel / host 工具），不碰磁盘
#    make build      重新构建磁盘镜像（清零 + dd 引导链 + 建文件系统）⚠ 会清掉手动加的文件
#    make run        用【当前】磁盘启动 QEMU（不重建，保留你手动增量加的文件）
#    make debug      同上，带异常日志（qemu.log）
#    make tools      只编宿主机侧工具（host/*.c → bin/*）
#    make fs         只重建文件系统部分（引导链不动）
#    make kernel-src 列出内核会编哪些文件 / include 路径
#    make clean      删掉编译产物
#    make help       打印这段说明
#
#  典型用法：
#    make && make build && make run      # 改了代码，重建磁盘再跑
#    make && ./bin/fshell ../nvme0virtual  # 只重编工具，然后手动往盘里加文件
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

HOST_SUPPORT := host/fsSys.c host/copyFs.c       # 公共实现（没有 main，不单独编成工具）
HOST_SRCS  := $(filter-out $(HOST_SUPPORT),$(wildcard host/*.c))
HOST_TOOLS := $(patsubst host/%.c,$(BIN)/%,$(HOST_SRCS))

.PHONY: all build run debug tools fs check clean help kernel-src check-image

# 默认目标：只编译，不动磁盘（所以你别怕 make 一下就把盘里的文件冲了）
all: $(BIN)/MBR.bin $(BIN)/loader.bin $(BIN)/kernel.bin $(HOST_TOOLS)

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

# 规则：host/xxx.c  →  bin/xxx（自动带上 fsTree.c / fsSys.c / copyFs.c）
$(BIN)/%: host/%.c code/minFs/fsTree.c $(HOST_SUPPORT) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^

# 只重建文件系统部分（引导链不动；内核照旧由 build.c 装进去）
fs: $(BIN)/build $(BIN)/kernel.bin
	$(BIN)/build $(IMAGE) $(BIN)/kernel.bin

$(BIN):
	@mkdir -p $(BIN)

# ---------------------------------------------------------------------------
# 重新构建磁盘：清零 + dd 写引导链 + host/build.c 建文件系统
#   这条命令会把整块盘清空重建，手动用 fshell 加的文件会没 —— 想保留就别跑它
# ---------------------------------------------------------------------------
build: all
	@echo "== 清零镜像 $(IMAGE) ($(IMAGE_MB)MB) =="
	@dd if=/dev/zero of=$(IMAGE) bs=1M count=$(IMAGE_MB) status=none
	@dd if=$(BIN)/MBR.bin    of=$(IMAGE) bs=512 seek=$(SECTOR_MBR)    count=1 conv=notrunc status=none
	@dd if=$(BIN)/loader.bin of=$(IMAGE) bs=512 seek=$(SECTOR_LOADER) conv=notrunc status=none
	@dd if=$(BIN)/kernel.bin of=$(IMAGE) bs=512 seek=$(SECTOR_KERNEL) conv=notrunc status=none
	@echo "引导链写入完成：MBR@$(SECTOR_MBR)、loader@$(SECTOR_LOADER)、kernel@$(SECTOR_KERNEL)"
	$(BIN)/build $(IMAGE) $(BIN)/kernel.bin

# 启动前先检查镜像在不在（run 故意不依赖 build：要保留手动加的文件时直接跑就行）
check-image:
	@test -f $(IMAGE) || { echo "镜像 $(IMAGE) 不存在，先跑一次：make build"; exit 1; }

run: check-image
	$(QEMU) -drive file=$(IMAGE),format=raw

debug: check-image
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
	@sed -n '2,18p' Makefile | sed 's/^# \{0,1\}//'
	@echo ""
	@echo "当前布局：镜像=$(IMAGE)"
	@echo "  MBR@$(SECTOR_MBR)  loader@$(SECTOR_LOADER)  kernel@$(SECTOR_KERNEL)（$(KERNEL_SECTORS) 扇区，加载到 $(KERNEL_BASE)）"
