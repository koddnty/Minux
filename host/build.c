/* ===========================================================================
 *  镜像安装器：把引导链 + 文件系统一起装进 nvme0virtual
 *
 *  用法：
 *    build <镜像> <MBR.bin> <loader.bin> <kernel.bin>   # 全量组装：建镜像→写引导链→建FS
 *    build --fs-only <镜像>                             # 只重建文件系统部分
 *
 *  repair: 原来这些活是「Makefile 里三条 dd + 这个 C 程序」，两边各写一份扇区号，
 *  repair: 一改就漂（内核那条 dd 被注释掉之后就出现"内核不见了"）。
 *  repair: 现在全部收在这一个程序里：引导链按原始扇区写（等价于 dd），
 *  repair: 文件系统用 fsTree 建树，两边共用同一套扇区号。
 *
 *  repair: 另外修了两处编译不过的地方：
 *  repair:   * fsSys.h / copyFs.h 里被 IDE 塞了 <c++/13/cstdio>，gcc 编 C 直接 fatal error
 *  repair:   * miCopyFile 的第二个参数是【父目录】不是完整路径（见它的注释）
 * ===========================================================================*/
#include "fsSys.h"
#include "minFs/fsTree.h"
#include "copyFs.h"

#include <stdio.h>
#include <string.h>

/* ---- 扇区布局（Makefile 会用 -D 传进来，这里给的是同样的默认值） ---- */
#ifndef SECTOR_MBR
#define SECTOR_MBR      0       /* MBR */
#endif
#ifndef SECTOR_LOADER
#define SECTOR_LOADER   3       /* loader；MBR 从这里连续读 16 个扇区 */
#endif
#ifndef SECTOR_KERNEL
#define SECTOR_KERNEL   32      /* 内核；loader 从这里读 */
#endif
#ifndef FS_DATA_START
#define FS_DATA_START   64      /* 文件数据/子目录节点的起跳扇区 */
#endif
#ifndef IMAGE_MB
#define IMAGE_MB        128
#endif

/* 子目录节点/文件数据放哪：最简单的 bump 分配，从 FS_DATA_START 往后排 */
static uint32_t gNextFree = FS_DATA_START;

static uint32_t allocSectors(uint32_t count) {
    uint32_t s = gNextFree;
    gNextFree += (count ? count : 1);
    return s;
}

static uint32_t sectorsOf(uint32_t bytes) {
    return (bytes + FS_SECTOR_SIZE - 1) / FS_SECTOR_SIZE;
}

/* 把根目录列出来，make 完立刻自检 */
static void listRoot(void) {
    struct FsTreeNode node = {0};

    if (miCoreReadTreeNode(FS_ROOT_SECTOR, &node) != 0) {
        printf("  (读根节点失败)\n");
        return;
    }

    if (node.child_count == 0) {
        printf("  (空目录)\n");
        return;
    }

    for (uint32_t i = 0; i < node.child_count; i++) {
        printf("  %c %-12s begin=%-5u len=%u\n",
               node.type[i], node.name[i], node.begin[i], node.length[i]);
    }
}

/* 把一个本地文件按【原始扇区】写到 lba，等价于
 *   dd if=<path> of=<镜像> bs=512 seek=<lba> conv=notrunc */
static int writeRawSectors(const char* path, uint32_t lba) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        printf("打不开 %s\n", path);
        return -1;
    }

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long size = ftell(f);
    rewind(f);

    if (size < 0) { fclose(f); return -1; }

    char buf[FS_SECTOR_SIZE];
    uint32_t sectors = sectorsOf((uint32_t)size);
    size_t written = 0;

    for (uint32_t i = 0; i < sectors; i++) {
        memset(buf, 0, sizeof(buf));                 /* 最后一个扇区补 0 */

        size_t want = (size_t)size - written;
        if (want > FS_SECTOR_SIZE) {
            want = FS_SECTOR_SIZE;
        }

        if (fread(buf, 1, want, f) != want) { fclose(f); printf("读 %s 出错\n", path); return -1; }
        written += want;

        if (miWriteSector(lba + i, buf, 1) != 0) { fclose(f); printf("写扇区 %u 失败\n", lba + i); return -1; }
    }

    fclose(f);
    printf("  %-20s -> 扇区 %-5u (%ld 字节, %u 扇区)\n", path, lba, size, sectors);
    return 0;
}

/* 在 FS 的 dir 目录里新建一个内容来自内存的文件（自己分配扇区） */
static int installBuffer(const char* dir, const char* name, const char* data, uint32_t len) {
    uint32_t sector = allocSectors(sectorsOf(len));
    uint32_t size = len;

    if (miCoreWriteFile((char*)dir, (char*)name, (char*)data, &size, sector) != 0) {
        return -1;
    }

    printf("  %s%s -> 扇区 %u (%u 字节)\n", dir, name, sector, len);
    return 0;
}

int main(int argc, char** argv) {
    const char* image;
    const char* mbr = NULL;
    const char* loader = NULL;
    const char* kernel = NULL;
    int fsOnly = 0;

    if (argc >= 2 && strcmp(argv[1], "--fs-only") == 0) {
        if (argc < 3) { printf("用法: %s --fs-only <镜像> [kernel.bin]\n", argv[0]); return 2; }
        fsOnly = 1;
        image = argv[2];
        kernel = (argc >= 4) ? argv[3] : NULL;      /* 传了就把它登记成 /kernel.bin */
    } else {
        if (argc < 5) {
            printf("用法: %s <镜像> <MBR.bin> <loader.bin> <kernel.bin>\n", argv[0]);
            printf("      %s --fs-only <镜像>\n", argv[0]);
            return 2;
        }
        image = argv[1];
        mbr = argv[2];
        loader = argv[3];
        kernel = argv[4];
    }

    /* 1) 建镜像 —— 替代 dd if=/dev/zero */
    if (!fsOnly) {
        if (miDiskCreate(image, (uint64_t)IMAGE_MB * 1024 * 1024) != 0) {
            perror(image);
            return 1;
        }
        printf("镜像已创建/清空：%s (%d MB)\n", image, IMAGE_MB);

        /* 2) 引导链：原始扇区写入 —— 替代原来的三条 dd */
        printf("写入引导链：\n");
        if (writeRawSectors(mbr, SECTOR_MBR) != 0) { miDiskClose(); return 1; }
        if (writeRawSectors(loader, SECTOR_LOADER) != 0) { miDiskClose(); return 1; }
        if (writeRawSectors(kernel, SECTOR_KERNEL) != 0) { miDiskClose(); return 1; }
    } else {
        if (miDiskOpen(image) != 0) { perror(image); return 1; }
    }

    /* 3) 文件系统 */
    printf("构建文件系统：\n");

    struct FsTreeNode root = {0};
    if (miCoreWriteTreeNode(FS_ROOT_SECTOR, &root) != 0) {
        printf("初始化根目录失败\n");
        miDiskClose();
        return 1;
    }

    /* 内核同时也登记成一个文件：数据还是引导用的那份（扇区 SECTOR_KERNEL），
     * 这样将来 loader 可以直接从文件系统里读 /kernel.bin，而不用写死扇区号 */
    if (kernel != NULL) {
        if (miCopyFile(kernel, "/", SECTOR_KERNEL, "kernel.bin") != 0) {
            printf("把内核登记成 /kernel.bin 失败\n");
            miDiskClose();
            return 1;
        }
        printf("  /kernel.bin -> 扇区 %u（复用引导用的那份数据）\n", SECTOR_KERNEL);
    }

    /* 一个子目录 + 一个示例文件（不需要就删掉这两段） */
    uint32_t dirSector = allocSectors(1);
    if (miCoreMakeDir("/", "root", dirSector) != 0) {
        printf("创建 /root 失败\n");
        miDiskClose();
        return 1;
    }
    printf("  /root/ -> 扇区 %u\n", dirSector);

    const char text[] = "hello minux\n";
    if (installBuffer("/root/", "hello.txt", text, sizeof(text) - 1) != 0) {
        printf("写 /root/hello.txt 失败\n");
        miDiskClose();
        return 1;
    }

    /* 4) 自检：立刻读回来 */
    char buf[64] = {0};
    uint32_t size = sizeof(buf);
    int got = miCoreReadFile("/root/hello.txt", buf, &size);

    printf("根目录内容：\n");
    listRoot();

    if (got < 0) {
        printf("回读 /root/hello.txt 失败\n");
        miDiskClose();
        return 1;
    }
    printf("  /root/hello.txt 回读 %d 字节：%s", got, buf);

    miDiskClose();
    return 0;
}
