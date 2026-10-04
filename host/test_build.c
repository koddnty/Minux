/* 把测试程序装进镜像的 /test/ 目录。
 *
 * 是【库】：没有 main，由 host/build.c 在同一个 miDiskOpen 会话里调用。
 * 拆成两个可执行文件各自 miDiskOpen 的话，后开的那个会覆盖前一个的结果。
 */
#include "test_build.h"

#include <stdio.h>

#include "copyFs.h"
#include "minFs/fsTree.h"

/* 扇区分配（磁盘布局见顶层 Makefile）：
 *   MBR@0 | loader@3..18 | 内核@32..63(KERNEL_SECTORS=32) | 后面留给文件系统数据
 * 所以 /test 的目录节点从 64 起，测试程序数据从 65 起往后排。
 * 都压在 100（fshell 的 FIRST_FREE_SECTOR）之前，免得手动加文件时撞上。
 */
#define MI_TEST_DIR_SECTOR  64u
#define MI_TEST_DATA_SECTOR 65u

/* 一个测试程序：宿主机上的裸二进制 + 它在 minux 里的名字 */
struct MiTestProgram {
    const char* local;      /* 形如 test/bin/hellominux.bin */
    const char* name;       /* minux 里的 /test/<name> */
};

/* 加测试程序的流程：test/<名字>.asm -> test/Makefile 的 PROGS -> 这里加一行 */
static const struct MiTestProgram mi_test_programs[] = {
    { "test/bin/hellominux.bin", "hellominux" },
};

/* 本地文件占几个扇区；失败/空文件返回 0 */
static uint32_t miTestFileSectors(const char* path) {
    FILE* file = fopen(path, "rb");

    if (file == NULL) {
        return 0;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }

    long size = ftell(file);
    fclose(file);

    if (size <= 0) {
        return 0;
    }

    return ((uint32_t)size + FS_SECTOR_SIZE - 1) / FS_SECTOR_SIZE;
}

int miTestBuildInstall(void) {
    if (miCoreMakeDir("/", "test", MI_TEST_DIR_SECTOR) != 0) {
        printf("创建 /test 失败\n");
        return -1;
    }

    uint32_t sector = MI_TEST_DATA_SECTOR;
    size_t count = sizeof(mi_test_programs) / sizeof(mi_test_programs[0]);

    for (size_t i = 0; i < count; i++) {
        const char* local = mi_test_programs[i].local;
        const char* name = mi_test_programs[i].name;

        uint32_t sectors = miTestFileSectors(local);

        if (sectors == 0) {
            printf("读不到 %s —— 先编测试程序: make test（或 cd test && make）\n", local);
            return -1;
        }

        if (miCopyFile(local, "/test", sector, name) != 0) {
            printf("装入 /test/%s 失败\n", name);
            return -1;
        }

        printf("  /test/%-12s <- %-24s %u 扇区 @%u\n", name, local, sectors, sector);

        sector += sectors;
    }

    return 0;
}
