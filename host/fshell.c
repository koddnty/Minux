/* ===========================================================================
 *  fshell —— 直接对着镜像文件操作 minFs 的交互式命令行
 *
 *  用法:  bin/fshell <镜像>
 *
 *  为什么不用新的 API：这个工具只调用现有的这些函数
 *      fsSys.h   : miDiskOpen / miDiskClose / miReadSector / miWriteSector
 *      fsTree.h  : miPathParser / miCoreReadTreeNode / miCoreWriteTreeNode
 *                  miCoreMakeDir / miCoreReadFile / miCoreWriteFile
 *      copyFs.h  : miCopyFile
 *  扇区分配、路径查找这些"工具自己的事"都在本文件里用上面这些函数算出来，
 *  没有往文件系统里加任何新东西。
 *
 *  扇区从哪来：没有空闲块表，所以每次都用「扫一遍整棵树」找出已被占用的最大扇区，
 *  再从它后面分配。这样不依赖工具上次运行的状态，随时手动增量更新都不会撞车。
 *  FIRST_FREE_SECTOR 是给引导链留的底：MBR@0、loader@3~18、内核@32~47。
 * ===========================================================================*/
#include "fsSys.h"
#include "minFs/fsTree.h"
#include "copyFs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIRST_FREE_SECTOR 100u
#define MAX_WALK_DEPTH    8u
#define CAT_BUF_SIZE      (64u * 1024u)

static char gCwd[256] = "/";

/* ---------- 小工具 ---------- */

static uint32_t sectorsOf(uint32_t bytes) {
    return (bytes + FS_SECTOR_SIZE - 1) / FS_SECTOR_SIZE;
}

/* 有长度保护的拼接（手写循环，避免 strncat 的截断警告） */
static void appendStr(char* dst, size_t cap, const char* s) {
    size_t used = strlen(dst);
    size_t i = 0;

    while (s[i] != '\0' && used + i + 1 < cap) {
        dst[used + i] = s[i];
        i++;
    }
    dst[used + i] = '\0';
}

/* 把当前目录 + 相对路径拼成绝对路径 */
static void absolutePath(const char* path, char* out, size_t outSize) {
    out[0] = '\0';                               /* appendStr 靠 strlen 找结尾，先清干净 */

    if (path == NULL || path[0] == '\0' || strcmp(path, ".") == 0) {
        appendStr(out, outSize, gCwd);           /* "." / 空 就是当前目录 */
        return;
    }

    if (strcmp(path, "..") == 0) {               /* ".." 回到上一层 */
        size_t n = strlen(gCwd);
        while (n > 1 && gCwd[n - 1] != '/') {
            n--;
        }
        if (n > 1) {
            n--;                                 /* 去掉结尾的 '/' */
        }
        for (size_t i = 0; i < n; i++) {
            out[i] = gCwd[i];
        }
        out[n] = '\0';
        if (out[0] == '\0') {
            appendStr(out, outSize, "/");
        }
        return;
    }

    if (path[0] == '/') {
        appendStr(out, outSize, path);
        return;
    }

    appendStr(out, outSize, gCwd);
    if (strcmp(gCwd, "/") != 0) {
        appendStr(out, outSize, "/");
    }
    appendStr(out, outSize, path);
}

/* 把 /a/b/c 拆成 父目录 "/a/b" 和 名字 "c"；返回 0 成功，-1 失败 */
static int splitParent(const char* absPath, char* parent, size_t parentSize, char* name, size_t nameSize) {
    char parts[FS_MAX_CHILD][FS_NAME_LEN];
    int n = miPathParser((char*)absPath, parts);

    if (n <= 0) {                            /* 根目录或解析失败 */
        return -1;
    }

    strncpy(name, parts[n - 1], nameSize - 1); name[nameSize - 1] = '\0';

    if (n == 1) {
        strncpy(parent, "/", parentSize - 1);
        parent[parentSize - 1] = '\0';
        return 0;
    }

    parent[0] = '\0';
    for (int i = 0; i < n - 1; i++) {
        appendStr(parent, parentSize, "/");
        appendStr(parent, parentSize, parts[i]);
    }
    return 0;
}

/* 在 node 里找名字：成功 0 并回填下标，找不到 -1（fsTree 里的同名函数是 static，所以这儿自己写） */
static int findChild(struct FsTreeNode* node, const char* name, uint32_t* index) {
    for (uint32_t i = 0; i < node->child_count; i++) {
        if (strcmp(node->name[i], name) == 0) {
            *index = i;
            return 0;
        }
    }
    return -1;
}

/* 解析绝对路径 → 该路径对应的目录节点。成功 0；-1 不存在/不是目录 */
static int resolveDir(const char* absPath, struct FsTreeNode* out) {
    char parts[FS_MAX_CHILD][FS_NAME_LEN];
    int n = miPathParser((char*)absPath, parts);

    if (n < 0) {
        return -1;
    }

    struct FsTreeNode node = {0};
    if (miCoreReadTreeNode(FS_ROOT_SECTOR, &node) != 0) {
        return -1;
    }

    for (int i = 0; i < n; i++) {
        uint32_t idx = 0;
        if (findChild(&node, parts[i], &idx) != 0) {
            return -1;
        }
        if (node.type[idx] != 'd') {
            return -1;
        }
        if (miCoreReadTreeNode(node.begin[idx], &node) != 0) {
            return -1;
        }
    }

    *out = node;
    return 0;
}

/* 遍历整棵树，找出「已被占用的最大扇区 + 1」，再和 FIRST_FREE_SECTOR 取大 */
static uint32_t scanMaxUsed(uint32_t dirSector, uint32_t depth) {
    struct FsTreeNode node = {0};
    uint32_t maxUsed = 0;

    if (depth > MAX_WALK_DEPTH) {
        return 0;
    }
    if (miCoreReadTreeNode(dirSector, &node) != 0) {
        return 0;
    }

    if (dirSector + 1 > maxUsed) {           /* 目录节点自己占一个扇区 */
        maxUsed = dirSector + 1;
    }

    for (uint32_t i = 0; i < node.child_count; i++) {
        uint32_t end = 0;
        uint32_t e;

        if (node.type[i] == 'd') {
            e = scanMaxUsed(node.begin[i], depth + 1);
        } else {
            e = node.begin[i] + ((node.length[i] == 0) ? 1 : sectorsOf(node.length[i]));
        }

        end = e;
        if (end > maxUsed) {
            maxUsed = end;
        }
    }

    return maxUsed;
}

static uint32_t nextFreeSector(void) {
    uint32_t used = scanMaxUsed(FS_ROOT_SECTOR, 0);
    return used > FIRST_FREE_SECTOR ? used : FIRST_FREE_SECTOR;
}

/* ---------- 命令 ---------- */

static void cmdLs(const char* path) {
    char abs[256];
    struct FsTreeNode node = {0};

    absolutePath(path && *path ? path : ".", abs, sizeof(abs));

    if (resolveDir(abs, &node) != 0) {
        printf("ls: 没有这个目录: %s\n", abs);
        return;
    }

    if (node.child_count == 0) {
        printf("(空目录)\n");
        return;
    }

    for (uint32_t i = 0; i < node.child_count; i++) {
        printf("  %c  %-14s begin=%-6u len=%u\n",
               node.type[i], node.name[i], node.begin[i], node.length[i]);
    }
}

static void cmdCd(const char* path) {
    char abs[256];
    struct FsTreeNode node = {0};

    absolutePath(path && *path ? path : "/", abs, sizeof(abs));

    if (resolveDir(abs, &node) != 0) {
        printf("cd: 没有这个目录: %s\n", abs);
        return;
    }

    strncpy(gCwd, abs, sizeof(gCwd) - 1);
    gCwd[sizeof(gCwd) - 1] = '\0';
}

static void cmdMkdir(const char* path) {
    char abs[256];
    char parent[256];
    char name[FS_NAME_LEN];
    uint32_t sector;

    if (!path || !*path) {
        printf("用法: mkdir <路径>\n");
        return;
    }

    absolutePath(path, abs, sizeof(abs));

    if (splitParent(abs, parent, sizeof(parent), name, sizeof(name)) != 0) {
        printf("mkdir: 路径不对: %s\n", abs);
        return;
    }
    if (strlen(name) >= FS_NAME_LEN) {
        printf("mkdir: 名字太长（最多 %d 字节）\n", FS_NAME_LEN - 1);
        return;
    }

    sector = nextFreeSector();

    if (miCoreMakeDir(parent, name, sector) != 0) {
        printf("mkdir: 失败（父目录不存在、或目录项已满 %d 个）\n", FS_MAX_CHILD);
        return;
    }

    printf("已创建 %s （目录节点在扇区 %u）\n", abs, sector);
}

static void cmdPut(const char* local, const char* remote) {
    char abs[256];
    char parent[256];
    char name[FS_NAME_LEN];
    FILE* f;
    long size;
    uint32_t sector;

    if (!local || !*local || !remote || !*remote) {
        printf("用法: put <本地文件> <镜像里的路径>\n");
        return;
    }

    f = fopen(local, "rb");
    if (f == NULL) {
        printf("put: 打不开本地文件 %s\n", local);
        return;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0) {
        fclose(f);
        printf("put: 读不了 %s\n", local);
        return;
    }
    fclose(f);

    absolutePath(remote, abs, sizeof(abs));

    if (splitParent(abs, parent, sizeof(parent), name, sizeof(name)) != 0) {
        printf("put: 目标路径不对: %s\n", abs);
        return;
    }
    if (strlen(name) >= FS_NAME_LEN) {
        printf("put: 名字太长（最多 %d 字节）\n", FS_NAME_LEN - 1);
        return;
    }

    sector = nextFreeSector();

    if (miCopyFile(local, parent, sector, name) != 0) {
        printf("put: 失败（父目录不存在、或目录项已满 %d 个）\n", FS_MAX_CHILD);
        return;
    }

    printf("已写入 %s （%ld 字节 -> 扇区 %u 起，占 %u 个扇区）\n",
           abs, size, sector, (unsigned)sectorsOf((uint32_t)size));
}

static void cmdCat(const char* path) {
    char abs[256];
    char* buf;
    uint32_t size;
    int got;

    if (!path || !*path) {
        printf("用法: cat <路径>\n");
        return;
    }

    absolutePath(path, abs, sizeof(abs));

    buf = malloc(CAT_BUF_SIZE);
    if (buf == NULL) {
        printf("cat: 内存不够\n");
        return;
    }

    size = CAT_BUF_SIZE;
    got = miCoreReadFile(abs, buf, &size);       /* 成功返回字节数（>=0），失败 -1 */

    if (got < 0) {
        printf("cat: 读不到 %s（不存在 / 是目录 / 缓冲区不够）\n", abs);
        free(buf);
        return;
    }

    fwrite(buf, 1, (size_t)got, stdout);
    if (got == 0 || buf[got - 1] != '\n') {
        putchar('\n');
    }

    free(buf);
}

static void cmdTree(uint32_t dirSector, const char* prefix, uint32_t depth) {
    struct FsTreeNode node = {0};
    char childPath[256];

    if (depth > MAX_WALK_DEPTH || miCoreReadTreeNode(dirSector, &node) != 0) {
        return;
    }

    for (uint32_t i = 0; i < node.child_count; i++) {
        printf("%s%c %s", prefix, node.type[i], node.name[i]);

        if (node.type[i] == 'd') {
            printf("/\n");
            snprintf(childPath, sizeof(childPath), "%s  ", prefix);
            cmdTree(node.begin[i], childPath, depth + 1);
        } else {
            printf("  (扇区 %u, %u 字节)\n", node.begin[i], node.length[i]);
        }
    }
}

static void cmdHelp(void) {
    printf("命令：\n"
           "  ls [路径]                列目录（默认当前目录）\n"
           "  cd <路径>                切换当前目录\n"
           "  pwd                      显示当前目录\n"
           "  tree                     打印整棵树\n"
           "  mkdir <路径>             创建目录\n"
           "  put <本地文件> <路径>     把宿主机文件写进镜像\n"
           "  cat <路径>               打印文件内容\n"
           "  free                     显示下一个可用扇区\n"
           "  help / exit\n");
}

static void cmdFree(void) {
    printf("下一个可用扇区: %u（已占用的最大扇区是扫描整棵树算出来的）\n", nextFreeSector());
}

/* ---------- 主循环 ---------- */

int main(int argc, char** argv) {
    char line[512];

    if (argc < 2) {
        printf("用法: %s <镜像>\n", argv[0]);
        printf("例如: %s ../nvme0virtual\n", argv[0]);
        return 2;
    }

    if (miDiskOpen(argv[1]) != 0) {
        perror(argv[1]);
        return 1;
    }

    printf("minFs shell —— 镜像 %s\n", argv[1]);
    printf("输入 help 看命令，exit 退出\n");

    for (;;) {
        char* cmd;
        char* arg1;
        char* arg2;
        char* save = NULL;

        printf("minux:%s> ", gCwd);
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;                                  /* Ctrl-D */
        }

        line[strcspn(line, "\r\n")] = '\0';

        cmd = strtok_r(line, " \t", &save);
        if (cmd == NULL) {
            continue;
        }
        arg1 = strtok_r(NULL, " \t", &save);
        arg2 = strtok_r(NULL, " \t", &save);

        if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
            break;
        } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
            cmdHelp();
        } else if (strcmp(cmd, "ls") == 0) {
            cmdLs(arg1);
        } else if (strcmp(cmd, "cd") == 0) {
            cmdCd(arg1);
        } else if (strcmp(cmd, "pwd") == 0) {
            printf("%s\n", gCwd);
        } else if (strcmp(cmd, "tree") == 0) {
            cmdTree(FS_ROOT_SECTOR, "", 0);
        } else if (strcmp(cmd, "mkdir") == 0) {
            cmdMkdir(arg1);
        } else if (strcmp(cmd, "put") == 0) {
            cmdPut(arg1, arg2);
        } else if (strcmp(cmd, "cat") == 0) {
            cmdCat(arg1);
        } else if (strcmp(cmd, "free") == 0) {
            cmdFree();
        } else {
            printf("未知命令: %s（输入 help 看用法）\n", cmd);
        }
    }

    miDiskClose();
    printf("bye\n");
    return 0;
}
