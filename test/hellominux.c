/* hellominux 的测试逻辑: 调 write 系统调用。
 *
 * write() 的实现在 code/kernel/lib/syscall.c（用户侧运行时, test/Makefile 会把它一起链进来）,
 * 内核侧由 ksyscall.asm 的 syscall_table[1] -> kernel_write_entry -> kernel_write() 处理。
 */
#include "kernel/lib/syscall.h"

int main(void) {
    write("hello minux\n");
    write("ok, i know\n");

    return 0;
}
