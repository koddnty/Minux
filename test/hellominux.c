/* hellominux 的测试逻辑: 调 write 系统调用。
 *
 * write() 的实现在 code/kernel/syscall/syscall.c（用户侧运行时, test/Makefile 会把它一起链进来）,
 * 内核侧由 ksyscall.asm 的 syscall_table[1] -> kernel_write_entry -> kernel_write() 处理。
 */
#include "kernel/base.h"
#include "kernel/syscall/syscall.h"
#include "kernel/syscall/threads.h"

void mi_int_to_string(int num, char* str)
{
    int i = 0;
    int negative = 0;

    if (num < 0) {
        negative = 1;
        num = -num;
    }

    do {
        str[i++] = num % 10 + '0';
        num /= 10;
    } while (num > 0);

    if (negative) {
        str[i++] = '-';
    }

    str[i] = '\0';

    for (int j = 0; j < i / 2; j++) {
        char temp = str[j];
        str[j] = str[i - j - 1];
        str[i - j - 1] = temp;
    }
}


int main(void) {
    write("hello minux\n");
    write("ok, i know\n");
    pid_t old_pid = get_pid();
    write("i will fork a new process, curr pid=");
    char pid_s[16] = "\0";
    mi_int_to_string(old_pid, pid_s);
    write(pid_s);
    write("\n");

    pid_t pid = fork();
    if (pid != 0) {
        // 当前进程
        write("curr process is old process\n");
    }
    else {
        // 新进程
        write("curr process is new process\n");
    }
    return 0;
}
