#pragma once

#include <stdint.h>
#include <stdio.h>      /* repair: 原来是 <c++/13/cstdio>（C++ 头），gcc 编 C 会 fatal error */
#include <stdlib.h>

#include "minFs/fsTree.h"

/* 把一个本地文件写进镜像的文件系统：
 *   local_path 宿主机上的文件（比如 bin/kernel.bin）
 *   disk_path  镜像里的父目录（"/" 表示根目录）
 *   sector     这个文件的数据放在哪个扇区开始（自己分配，不做冲突检测）
 *   name       在文件系统里的名字
 * 成功 0；失败 -1（文件打不开、分配失败、扇区写失败…）
 */
int miCopyFile(const char* local_path, const char* disk_path, uint32_t sector, const char* name);
