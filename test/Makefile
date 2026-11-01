# ============================================================================
#  test/ 的独立 Makefile —— 只干一件事: 把测试程序编成【裸二进制】
#
#    顶层 make 会通过 $(MAKE) -C test 调它（见顶层 Makefile 的 test-progs 目标）,
#    也可以进来单独编:  cd test && make
#
#  产物: test/bin/<名字>.bin
#    这些 bin 由 host/test_build.c 装进 minux 的 /test/ 目录,
#    内核再用 miCoreProgramLoader() 加载到 0x40000000 跑起来。
#
#  为什么是裸二进制而不是 ELF?
#    miCoreProgramLoader 把文件【原样】拷到 0x40000000 然后跳过去执行, 它不解析 ELF 头。
#    所以必须交裸二进制, 并且入口 _start 要正好落在偏移 0 —— test.ld 里
#    `. = 0x40000000` 保证了这一点（objcopy -O binary 以最低 LMA 为文件起点）。
#    直接 `ld -o prog` 出来的 ELF 是没法用的: 第 0 字节是 \x7fELF。
#
#  加一个新测试程序:
#    1) 写 <名字>.asm（入口符号叫 _start）
#    2) 需要 C 就再写 <名字>.c（会被自动编进来）
#    3) 把 <名字> 加到下面的 PROGS
#    4) 在 host/test_build.c 的表里加一行, 不然不会装进镜像
# ============================================================================

NASM    := nasm
CC      := gcc
LD      := ld
OBJCOPY := objcopy

SHELL   := /bin/bash     # check-entry 里用了 $((16#..))，dash 不认

ROOT    := $(CURDIR)/..

BIN     := bin

# 用户程序链接地址, 必须和 code/kernel/proc/kfork.h 的 USER_CODE_VADDR 一致
USER_BASE := 0x40000000

# 用户侧运行时: write() 这类系统调用包装。它是【用户程序】的一部分,
# 内核里那份编进去只是顺带（内核自己用 kVprintf），所以这里必须单独链进来,
# 否则 hellominux.c 里的 write() 会 undefined reference。
USER_RUNTIME_SRC := $(ROOT)/code/kernel/syscall/syscall.c

# 测试 C 代码: 没有 libc（内核只提供 code/tools/cstring.c 那几个函数）
#   -I $(ROOT)/code 是为了让测试代码能 #include "kernel/syscall/syscall.h"
TCFLAGS := -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin \
           -fno-asynchronous-unwind-tables -O2 -Wall -I $(ROOT)/code

# 每个名字一个测试程序, 产物 bin/<名字>.bin
PROGS   := hellominux
BINS    := $(addprefix $(BIN)/,$(addsuffix .bin,$(PROGS)))

# 每个程序的 C 源（存在才有）。注意不能在模式规则里写 $(wildcard %.c) ——
# 那里 % 是字面量, 展开出来是 "%.c" 这个不存在的文件名, 依赖就丢了,
# 改 .c 之后 make 不会重编。所以在这里用已知的 PROGS 显式列出来,
# 代价是改任意一个 .c 会重编全部测试程序（数量少, 无所谓）。
PROG_C  := $(wildcard $(addsuffix .c,$(PROGS)))

.PHONY: all clean list check-entry

all: $(BINS)

# <名字>.asm [+ <名字>.c] + 用户侧运行时 -> 链接 -> objcopy 出裸二进制
#   有 <名字>.c 就一起编进去, 没有就只链 asm。
$(BIN)/%.bin: %.asm test.ld $(PROG_C) $(USER_RUNTIME_SRC) | $(BIN)
	@mkdir -p $(BIN)
	@set -e; \
	$(NASM) -f elf32 $< -o $(BIN)/$*.asm.o; \
	objs="$(BIN)/$*.asm.o"; \
	$(CC) $(TCFLAGS) -c $(USER_RUNTIME_SRC) -o $(BIN)/$*.runtime.o; \
	objs="$$objs $(BIN)/$*.runtime.o"; \
	if [ -f "$*.c" ]; then \
	    $(CC) $(TCFLAGS) -c $*.c -o $(BIN)/$*.c.o; \
	    objs="$$objs $(BIN)/$*.c.o"; \
	fi; \
	$(LD) -m elf_i386 -T test.ld -o $(BIN)/$*.elf $$objs; \
	$(OBJCOPY) -O binary $(BIN)/$*.elf $@; \
	echo "  测试程序 $@: $$(stat -c %s $@) 字节"

# 检查产物是不是【偏移 0 就是入口】的裸二进制
#   miCoreProgramLoader 是硬性假设这个的; 一旦有人改成直接 ld 出 ELF, 这里会拦住。
check-entry: $(BINS)
	@set -e; for b in $(BINS); do \
	    e=$$(nm $(BIN)/$$(basename $$b .bin).elf | awk '/ T _start$$/ {print $$1}'); \
	    if [ "$$((16#$$e))" != "$$((16#400000))" ]; then \
	        echo "$$b: _start 在 0x$$e, 不是 0x40000000 —— test.ld 的 . = 0x40000000 被改过了?"; exit 1; \
	    fi; \
	    if xxd -l 4 $$b | grep -q "7f45 4c46"; then \
	        echo "$$b: 是 ELF 不是裸二进制, miCoreProgramLoader 跑不了它"; exit 1; \
	    fi; \
	    echo "  $$b: _start@0x40000000, 裸二进制 OK"; \
	done

# 看看某个程序里都有什么符号（排查入口没在偏移 0 很有用）
list: $(BINS)
	@for b in $(BINS); do \
	    echo "== $$b =="; \
	    nm $$b 2>/dev/null || true; \
	    xxd -l 16 $$b; \
	done

$(BIN):
	@mkdir -p $(BIN)

clean:
	rm -rf $(BIN)
