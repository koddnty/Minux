#include "fsSys.h"
#include "minFs/fsTree.h"
#include "copyFs.h"
#include <stdio.h>
#include <string.h>

/* repair: 恢复成你原来的写法（之前我擅自把它改成了"镜像安装器"，已回退）。
 * repair: 只修了两处让它能编、能跑的：
 *   * 内核文件路径原来是占位符 "root_path/bin/kernal.bin"（kernal 还拼错了），
 *     现在从命令行第二个参数取，Makefile 会传 bin/kernel.bin
 *   * 出错信息里的路径跟着改对
 */

static uint32_t SECTOR_KERNEL = 32;

/* 把根目录里的条目打印出来，方便 make 完立刻自检 */
static void listRoot(void) {
    struct FsTreeNode node = {0};

    if (miCoreReadTreeNode(FS_ROOT_SECTOR, &node) != 0) {
        printf("读根节点失败\n");
        return;
    }

    if (node.child_count == 0) {
        printf("空目录\n");
        return;
    }

    for (uint32_t i = 0; i < node.child_count; i++) {
        printf("  %c %-12s begin=%-5u len=%u\n",
               node.type[i], node.name[i], node.begin[i], node.length[i]);
    }
}

int main(int argc, char** argv) {
    const char* image = argc > 1 ? argv[1] : "../nvme0virtual";
    const char* kernel_path = argc > 2 ? argv[2] : "bin/kernel.bin";

    printf("building file system ...\n");

    if (miDiskOpen(image) != 0) {
        perror(image);
        return 1;
    }

    /*初始化根目录节点 */
    struct FsTreeNode root = {0};
    if (miCoreWriteTreeNode(FS_ROOT_SECTOR, &root) != 0) {
        printf("初始化根目录失败\n");
        miDiskClose();
        return 1;
    }

    /* 建一个子目录 /root */
    if (miCoreMakeDir("/", "root", 2) != 0) {
        printf("创建 /root 失败\n");
        miDiskClose();
        return 1;
    }
    if (miCoreMakeDir("/root", "kernel", SECTOR_KERNEL - 1) != 0) {
        printf("创建 /root/kernel 失败\n");
        miDiskClose();
        return 1;
    }

    printf("copy data...\n");

    /* 创建 kernel 文件。
     * 注意 miCopyFile(local, disk_path, sector, name) 的第二个参数是【父目录】，
     * 所以这一句会得到 /root/kernel/kernel；若想要 /root/kernel 直接传 "/root"。 */
    int rt = miCopyFile(kernel_path, "/root/kernel", SECTOR_KERNEL, "kernel");
    if (rt) {
        printf("failed to create /root/kernel/kernel\n");
        miDiskClose();
        return 1;
    }

    listRoot();

    miDiskClose();
    return 0;
}
