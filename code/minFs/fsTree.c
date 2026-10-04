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


// repair: 加了 offset(从文件第几字节开始读); buffer_size 现在只表示"缓冲区能装多少",
// repair: 不再是"入参容量/出参长度"的进出参, 实际读到多少字节由返回值给出。
int miCoreReadFile(char* path, char* buffer, uint32_t offset, uint32_t buffer_size) {
    uint32_t selector = FS_ROOT_SECTOR;
    struct FsTreeNode node = {};

    if (path == NULL || buffer == NULL) {
        return -1;
    }

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

            if (offset >= file_size) {
                return 0;               // repair: 起点已在文件尾之后, 一个字节都读不到
            }

            // repair: 能读的上限 = 文件剩余字节数, 再被调用方给的缓冲区容量卡一次
            uint32_t remain_in_file = file_size - offset;
            uint32_t need = remain_in_file < buffer_size ? remain_in_file : buffer_size;

            char sector_buffer[FS_SECTOR_SIZE];
            uint32_t done = 0;

            // repair: offset 可能落在扇区中间, 所以每轮只拷该扇区里该拷的那一段
            while (done < need) {
                uint32_t pos = offset + done;                   // 文件内偏移
                uint32_t in_sector = pos % FS_SECTOR_SIZE;      // 落在扇区里的位置

                if (miReadSector(begin + pos / FS_SECTOR_SIZE, sector_buffer, 1) != 0) {
                    return -1;
                }

                uint32_t chunk = FS_SECTOR_SIZE - in_sector;
                if (chunk > need - done) {
                    chunk = need - done;
                }

                memcpy(buffer + done, sector_buffer + in_sector, chunk);
                done += chunk;
            }

            return (int)done;       // 成功：返回实际读到的字节数（可能小于 buffer_size）
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
