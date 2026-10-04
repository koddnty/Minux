# ============================================================================
#  test/ 的独立 Makefile —— 只干一件事: 把测试程序编成【裸二进制】
#
#    顶层 make 会通过 $(MAKE) -C test 调它（见顶层 Makefile 的 test-progs 目标）,
#    也可以进来单独编:  cd test && make
#
#  产物: test/bin/<名字>.bin
#    这些 bin 由 host/test_build.c 装进 minux 的 /test/ 目录,
#    内核再用 miCoreProgramLoader() 加载到 0x400000 跑起来。
#
#  为什么是裸二进制而不是 ELF?
#    miCoreProgramLoader 把文件【原样】拷到 0x400000 然后跳过去执行, 它不解析 ELF 头。
#    所以必须交裸二进制, 并且入口 _start 要正好落在偏移 0 —— test.ld 里
#    `. = 0x400000` 保证了这一点（objcopy -O binary 以最低 LMA 为文件起点）。
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

BIN     := bin

# 用户程序链接地址, 必须和 code/kernel/lib/kfork.h 的 USER_CODE_VADDR 一致
USER_BASE := 0x400000

# 测试 C 代码: 没有 libc（内核只提供 code/tools/cstring.c 那几个函数）
TCFLAGS := -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin \
           -fno-asynchronous-unwind-tables -O2 -Wall

# 每个名字一个测试程序, 产物 bin/<名字>.bin
PROGS   := hellominux
BINS    := $(addprefix $(BIN)/,$(addsuffix .bin,$(PROGS)))

.PHONY: all clean list

all: $(BINS)

# <名字>.asm [+ <名字>.c] -> 链接 -> objcopy 出裸二进制
#   有 <名字>.c 就一起编进去, 没有就只链 asm。
$(BIN)/%.bin: %.asm test.ld | $(BIN)
	@mkdir -p $(BIN)
	@set -e; \
	objs="$(BIN)/$*.asm.o"; \
	$(NASM) -f elf32 $< -o $(BIN)/$*.asm.o; \
	if [ -f "$*.c" ]; then \
	    $(CC) $(TCFLAGS) -c $*.c -o $(BIN)/$*.c.o; \
	    objs="$$objs $(BIN)/$*.c.o"; \
	fi; \
	$(LD) -m elf_i386 -T test.ld -o $(BIN)/$*.elf $$objs; \
	$(OBJCOPY) -O binary $(BIN)/$*.elf $@; \
	echo "  测试程序 $@: $$(stat -c %s $@) 字节"

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
