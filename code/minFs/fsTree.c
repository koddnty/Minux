#include "minFs/fsTree.h"



#include <stdint.h>
#include <string.h>

/* repair: 全文件统一成「失败返回负数(-1)、成功返回 0 或正数」。
 *   原来 miCoreReadTreeNode / miCoreWriteTreeNode / miCoreFindChild 用的是
 *   「1=成功、0=失败」，和本文件里的 miCoreMakeDir / miCoreWriteFile（0=成功、-1=失败）
 *   以及 readSector（0=成功、-1=失败）混在一起，调用方极易写反
 *   （host/build.c 就写成了 if (!miCoreWriteTreeNode(...)) 打印“成功”）。
 *   判断失败请用 if (ret < 0) 或 if (ret != 0)。 */

// tools --------------------------------------------------
// 找到同名子节点：成功 0（并回填下标 index），失败 -1
static int miCoreFindChild(struct FsTreeNode* node, const char* name, uint32_t* index) {
    for (uint32_t i = 0; i < node->child_count; i++) {
        if (strcmp(node->name[i], name) == 0) {
            *index = i;
            return 0;
        }
    }

    return -1;
}
// "/a/b/c"
// 成功：路径段个数(>=0，根目录为 0)；失败：-1
int miPathParser(char* path, char buffer[FS_MAX_CHILD][FS_NAME_LEN]) {
    int count = 0;
    char* p = path;
    if (path == NULL || buffer == NULL)
        return -1;
    if (*p == '/')
        p++;
    while (*p != '\0' && count < FS_MAX_CHILD) {
        char* start = p;
        int len = 0;

        while (*p != '\0' && *p != '/') {
            p++;
            len++;
        }

        if (len >= FS_NAME_LEN)
            return -1;

        memcpy(buffer[count], start, len);
        buffer[count][len] = '\0';
        count++;

        while (*p == '/')
            p++;
    }

    return count;
}

// 成功：0；失败：-1
int miCoreReadTreeNode(uint32_t selector, struct FsTreeNode* node) {
    char buffer[FS_SECTOR_SIZE];
    uint32_t offset = 0;

    if (!node) {
        return -1;
    }

    // 从磁盘读取一个目录节点
    if (miReadSector(selector, buffer, 1) != 0) {
        return -1;
    }

    // 读取子节点数量
    memcpy(&node->child_count, buffer + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    if (node->child_count > FS_MAX_CHILD) {
        return -1;
    }

    // 读取每个子节点
    for (uint32_t i = 0; i < node->child_count; i++) {
        uint32_t name_len = strlen(buffer + offset);

        if (name_len >= FS_NAME_LEN) {
            return -1;
        }

        strcpy(node->name[i], buffer + offset);
        offset += name_len + 1;

        memcpy(&node->begin[i], buffer + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        memcpy(&node->length[i], buffer + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        memcpy(&node->type[i], buffer + offset, sizeof(char));
        offset += sizeof(char);
    }

    return 0;
}

// 成功：0；失败：-1
int miCoreWriteTreeNode(uint32_t selector, struct FsTreeNode* node) {
    char buffer[FS_SECTOR_SIZE];
    uint32_t offset = 0;

    if (!node) {
        return -1;
    }

    if (node->child_count > FS_MAX_CHILD) {
        return -1;
    }

    memset(buffer, 0, sizeof(buffer));

    // 写入子节点数量
    memcpy(buffer + offset, &node->child_count, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    // 写入每个子节点
    for (uint32_t i = 0; i < node->child_count; i++) {
        uint32_t name_len = strlen(node->name[i]);

        if (name_len >= FS_NAME_LEN) {
            return -1;
        }

        if (offset + name_len + 1 +
            sizeof(uint32_t) + sizeof(uint32_t) + sizeof(char) > FS_SECTOR_SIZE) {
            return -1;
        }

        strcpy(buffer + offset, node->name[i]);
        offset += name_len + 1;

        memcpy(buffer + offset, &node->begin[i], sizeof(uint32_t));
        offset += sizeof(uint32_t);

        memcpy(buffer + offset, &node->length[i], sizeof(uint32_t));
        offset += sizeof(uint32_t);

        memcpy(buffer + offset, &node->type[i], sizeof(char));
        offset += sizeof(char);
    }

    // 将整个目录节点写回一个扇区
    if (miWriteSector(selector, buffer, 1) != 0) {
        return -1;
    }

    return 0;
}


// 成功：0；失败：-1
int miCoreMakeDir(char* path, char* name, uint32_t sector) {
    struct FsTreeNode parent = {};

    char path_buffer[FS_MAX_CHILD][FS_NAME_LEN];
    int path_num = miPathParser(path, path_buffer);

    if (path_num < 0) {
        return -1;
    }

    uint32_t selector = FS_ROOT_SECTOR;

    if (miCoreReadTreeNode(selector, &parent) != 0) {
        return -1;
    }

    // 沿路径寻找父目录
    for (int level = 0; level < path_num; level++) {
        uint32_t index = 0;

        if (miCoreFindChild(&parent, path_buffer[level], &index) != 0) {
            return -1;
        }

        if (parent.type[index] != 'd') {
            return -1;
        }

        selector = parent.begin[index];

        if (miCoreReadTreeNode(selector, &parent) != 0) {
            return -1;
        }
    }

    // 当前节点必须是目录
    // 根节点也默认是目录
    if (parent.child_count >= FS_MAX_CHILD) {
        return -1;
    }

    uint32_t index = parent.child_count;

    strcpy(parent.name[index], name);
    parent.begin[index] = sector;
    parent.length[index] = 0;
    parent.type[index] = 'd';
    parent.child_count++;

    // 创建一个空目录节点
    struct FsTreeNode child = {};
    child.child_count = 0;

    if (miCoreWriteTreeNode(sector, &child) != 0) {
        return -1;
    }

    // 将新目录加入父目录
    if (miCoreWriteTreeNode(selector, &parent) != 0) {
        return -1;
    }

    return 0;
}

// repair: 原来只把文件长度写回 *buffer_size，数据一个字节都没读（下面那句注释 "从 node.begin[idx] 读取文件" 是空的）。
// repair: 现在真的把内容读进 buffer，并按新约定返回读到的字节数（>=0），失败 -1。
// repair: buffer_size 是「入参=容量、出参=实际长度」，容量不够返回 -1，不会写越界。
int miCoreReadFile(char* path, char* buffer, uint32_t* buffer_size) {
    uint32_t selector = FS_ROOT_SECTOR;
    struct FsTreeNode node = {};

    if (path == NULL || buffer == NULL || buffer_size == NULL) {
        return -1;
    }

    uint32_t capacity = *buffer_size;

    // 路径解析
    char path_buffer[FS_MAX_CHILD][FS_NAME_LEN];
    int path_num = miPathParser(path, path_buffer);
    if (path_num < 0) {
        return -1;
    }

    // 找文件
    if (miCoreReadTreeNode(selector, &node) != 0) {
        return -1;
    }

    for (int level = 0; level < path_num; level++) {
        uint32_t idx = 0;

        if (miCoreFindChild(&node, path_buffer[level], &idx) != 0) {
            return -1;
        }

        if (level == path_num - 1) {
            if (node.type[idx] != 'f') {
                return -1;
            }

            uint32_t file_size = node.length[idx];
            uint32_t begin = node.begin[idx];

            if (file_size > capacity) {
                return -1;              // 调用方的缓冲区装不下
            }

            // 一个扇区一个扇区地读，最后一块只拷贝 file_size 需要的部分
            char sector_buffer[FS_SECTOR_SIZE];
            uint32_t done = 0;
            uint32_t sector_count = (file_size + FS_SECTOR_SIZE - 1) / FS_SECTOR_SIZE;

            for (uint32_t i = 0; i < sector_count; i++) {
                if (miReadSector(begin + i, sector_buffer, 1) != 0) {
                    return -1;
                }

                uint32_t remain = file_size - done;
                uint32_t copy_size = remain > FS_SECTOR_SIZE ? FS_SECTOR_SIZE : remain;

                memcpy(buffer + done, sector_buffer, copy_size);
                done += copy_size;
            }

            *buffer_size = file_size;

            return (int)file_size;      // 成功：返回读到的字节数
        }

        if (node.type[idx] != 'd') {
            return -1;
        }

        if (miCoreReadTreeNode(node.begin[idx], &node) != 0) {
            return -1;
        }
    }

    return -1;
}

// 成功：0；失败：-1
int miCoreWriteFile(char* path, char* name, char* buffer, uint32_t* buffer_size, uint32_t sector) {
    struct FsTreeNode parent = {};

    char path_buffer[FS_MAX_CHILD][FS_NAME_LEN];
    int path_num = miPathParser(path, path_buffer);

    if (path_num < 0) {
        return -1;
    }

    uint32_t selector = FS_ROOT_SECTOR;

    if (miCoreReadTreeNode(selector, &parent) != 0) {
        return -1;
    }

    // 沿路径寻找父目录
    for (int level = 0; level < path_num; level++) {
        uint32_t index = 0;

        if (miCoreFindChild(&parent, path_buffer[level], &index) != 0) {
            return -1;
        }

        if (parent.type[index] != 'd') {
            return -1;
        }

        selector = parent.begin[index];

        if (miCoreReadTreeNode(selector, &parent) != 0) {
            return -1;
        }
    }

    // 当前节点必须还有子节点空间
    if (parent.child_count >= FS_MAX_CHILD) {
        return -1;
    }

    uint32_t file_size = *buffer_size;
    uint32_t sector_count = (file_size + FS_SECTOR_SIZE - 1) / FS_SECTOR_SIZE;

    // 写文件数据
    for (uint32_t i = 0; i < sector_count; i++) {
        char sector_buffer[FS_SECTOR_SIZE] = {};

        uint32_t offset = i * FS_SECTOR_SIZE;
        uint32_t remain = file_size - offset;
        uint32_t copy_size = remain > FS_SECTOR_SIZE
            ? FS_SECTOR_SIZE
            : remain;

        memcpy(sector_buffer, buffer + offset, copy_size);

        if (miWriteSector(sector + i, sector_buffer, 1) != 0) {
            return -1;
        }
    }

    // 将文件加入父目录
    uint32_t index = parent.child_count;

    strcpy(parent.name[index], name);
    parent.begin[index] = sector;
    parent.length[index] = file_size;
    parent.type[index] = 'f';
    parent.child_count++;

    if (miCoreWriteTreeNode(selector, &parent) != 0) {
        return -1;
    }

    return 0;
}
