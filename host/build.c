#include "fsSys.h"
#include "minFs/fsTree.h"
#include <stdio.h>
#include <string.h>

/* 把根目录里的条目打印出来，方便 make 完立刻自检 */
static void listRoot(void) {
    struct FsTreeNode node = {0};

    if (miCoreReadTreeNode(FS_ROOT_SECTOR, &node) != 0) {
        printf("  （读根节点失败）\n");
        return;
    }

    if (node.child_count == 0) {
        printf("  （空目录）\n");
        return;
    }

    for (uint32_t i = 0; i < node.child_count; i++) {
        printf("  %c %-12s begin=%-5u len=%u\n",
               node.type[i], node.name[i], node.begin[i], node.length[i]);
    }
}

int main(int argc, char** argv) {
    const char* image = argc > 1 ? argv[1] : "../nvme0virtual";

    if (miDiskOpen(image) != 0) {
        perror(image);
        return 1;
    }

    /* 1) 初始化根目录节点（一个扇区）。必须清零，别用栈上的垃圾数据 */
    struct FsTreeNode root = {0};
    if (miCoreWriteTreeNode(FS_ROOT_SECTOR, &root) != 0) {
        printf("初始化根目录失败\n");
        miDiskClose();
        return 1;
    }

    /* 2) 建一个子目录 /root（miCoreMakeDir 会顺手把它的空目录节点也写好） */
    if (miCoreMakeDir("/", "root", 2) != 0) {
        printf("创建 /root 失败\n");
        miDiskClose();
        return 1;
    }

    /* 3) 示例文件（不需要就删掉这段） */
    char text[] = "hello minux\n";
    uint32_t size = sizeof(text) - 1;
    if (miCoreWriteFile("/", "hello.txt", text, &size, 100) != 0) {
        printf("写入 /hello.txt 失败\n");
        miDiskClose();
        return 1;
    }

    /* 4) 自检：立刻读回来，确认盘上真的能用 */
    char     got_buf[64] = {0};
    uint32_t got_size = sizeof(got_buf);   /* 入参 = 缓冲区容量；出参 = 实际读到的字节数 */

    if (miCoreReadFile("/hello.txt", got_buf, &got_size) < 0) {
        printf("回读 /hello.txt 失败\n");
        miDiskClose();
        return 1;
    }

    printf("文件系统构建完成：\n");
    listRoot();
    printf("  /hello.txt 回读 %u 字节：%s", got_size, got_buf);

    miDiskClose();
    return 0;
}
