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
SECTOR_MBR     := 0
SECTOR_LOADER  := 3
IMAGE_MB       := 128

NASM      := nasm
CC        := gcc
NASMFLAGS := -f bin
CFLAGS    := -O2 -Wall -D_FILE_OFFSET_BITS=64 -I code -I host

HOST_SUPPORT := host/fsSys.c                    # 公共实现（没有 main，不单独编成工具）
HOST_SRCS  := $(filter-out $(HOST_SUPPORT),$(wildcard host/*.c))
HOST_TOOLS := $(patsubst host/%.c,$(BIN)/%,$(HOST_SRCS))

.PHONY: all image run debug tools fs check clean help

all: image

# ---------------------------------------------------------------------------
# 引导链
# ---------------------------------------------------------------------------
$(BIN)/MBR.bin: code/boot/MBR.asm | $(BIN)
	$(NASM) $(NASMFLAGS) $< -o $@

$(BIN)/loader.bin: code/boot/loader.asm | $(BIN)
	$(NASM) $(NASMFLAGS) $< -o $@

# 说明：这里用 `nasm -f bin` 而不是 elf32+ld+objcopy。
# loader.asm 里有多段（.s16/.gdt/.data1/.gs/.s32）并且靠绝对地址（PM_LOADER_CODE32
# 之类）在运行时回填描述符，-f bin 会严格按源码里的段顺序拼出扁平的二进制，
# 和代码里的地址假设一致；走 ld 的话段顺序由链接脚本决定，很容易对不上。

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
image: $(BIN)/MBR.bin $(BIN)/loader.bin $(BIN)/build
	@echo "== 清零镜像 $(IMAGE) ($(IMAGE_MB)MB) =="
	@dd if=/dev/zero of=$(IMAGE) bs=1M count=$(IMAGE_MB) status=none
	@dd if=$(BIN)/MBR.bin    of=$(IMAGE) bs=512 seek=$(SECTOR_MBR)    count=1 conv=notrunc status=none
	@dd if=$(BIN)/loader.bin of=$(IMAGE) bs=512 seek=$(SECTOR_LOADER) conv=notrunc status=none
	@echo "镜像组装完成：MBR@扇区$(SECTOR_MBR)、loader@扇区$(SECTOR_LOADER)"
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
	      $(HOST_TOOLS) qemu.log

help:
	@sed -n '2,13p' Makefile | sed 's/^# \{0,1\}//'
	@echo ""
	@echo "当前布局：镜像=$(IMAGE)  MBR@$(SECTOR_MBR)  loader@$(SECTOR_LOADER)"
