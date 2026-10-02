#pragma once
#include <stdint.h>
#include <string.h>
#include "readSector.h"

#define FS_SECTOR_SIZE 512
#define FS_MAX_CHILD 16
#define FS_NAME_LEN 16
#define FS_ROOT_SECTOR 1

/* ===========================================================================
 *  返回值约定（全项目统一，改代码前先看这里）
 * ---------------------------------------------------------------------------
 *    失败 -> 负数（统一用 -1）
 *    成功 -> 0 或正数
 *            0      : 成功，但没有额外信息要带回
 *            正数   : 成功，并带回一个量（字节数 / 路径段数 …）
 * ---------------------------------------------------------------------------
 *  repair: 原来两套约定并存：
 *            miCoreReadTreeNode / miCoreWriteTreeNode  是 1=成功、0=失败
 *            miCoreMakeDir / miCoreWriteFile / …       是 0=成功、-1=失败
 *          结果就是 host/build.c 里写成 if (!miCoreWriteTreeNode(...)) 打印"成功"，
 *          而它其实在失败时才打印 —— 信息刚好是反的。
 *          现在全部改成「失败负数、成功0或正数」，和 read/write/malloc 这些
 *          C 标准函数一致：
 *              调用方判断失败请用   if (ret < 0)  /  if (ret != 0)
 *              不要把 0 当成功以外的特殊含义，也不要用 if (!ret) 当成功判断
 * ===========================================================================*/

/**
 *  基础文件系统布局
 *
 *  一个目录 = 一个扇区（512B），内容布局：
 *    child_count(4B) | name[0]'\0' | begin[0](4B) | length[0](4B) | type[0](1B) | name[1]...
 *  文件内容从 begin 扇区开始连续存放，length 是字节数；目录的 length 为 0。
 */

// child_count | name[0] 0| begin[0] 0| length[0]0 | type[0] | name[1] 0| begin[1] ...
struct FsTreeNode {
    // file
    uint32_t child_count;                         // 目录文件个数
    char name[FS_MAX_CHILD][FS_NAME_LEN];       // 字符串指针，存储子节点的名称
    uint32_t begin[FS_MAX_CHILD];                 // 子节点扇区起始
    uint32_t length[FS_MAX_CHILD];                 // 子节点扇区起始
    char type[FS_MAX_CHILD];                    // 类型 f（文件, d（目录
};


/**
 * @brief 解析路径
 * @return 成功：路径名个数（>=0，根目录是 0）；失败：-1
 */
int miPathParser(char* path, char buffer[FS_MAX_CHILD][FS_NAME_LEN]);


/**
 * @brief 读/写一个目录节点（一个扇区）
 * @return 成功：0；失败：-1
 */
int miCoreReadTreeNode(uint32_t selector, struct FsTreeNode* node);
int miCoreWriteTreeNode(uint32_t selector, struct FsTreeNode* node);


/**
 * @brief 创建文件夹, 不支持嵌套
 * @param path   父目录路径（"/" 表示根目录）
 * @param name   新建目录名
 * @param sector 新目录节点占用的扇区
 * @return 成功：0；失败：-1
 */
int miCoreMakeDir(char* path, char* name, uint32_t sector);


/**
 * @brief 读取文件内容
 * @param buffer      输出缓冲区
 * @param buffer_size 入参=缓冲区容量；出参=实际读到的字节数
 * @return 成功：读到的字节数（>=0）；失败：-1
 * @note 读到的是原始字节，不会自动补 '\0'。要当字符串用，请自己保证
 *       buffer_size >= 文件长度+1 并把最后一个字节置 0。
 */
int miCoreReadFile(char* path, char* buffer, uint32_t* buffer_size);


/**
 * @brief 文件创建/写入, 文件直接放置在 sector 位置上并构建 treenode，不做冲突检测
 * @return 成功：0；失败：-1
 */
int miCoreWriteFile(char* path, char* name, char* buffer, uint32_t* buffer_size, uint32_t sector);
