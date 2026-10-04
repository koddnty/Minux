#pragma once

#include <stdint.h>

/* 把 test/ 下编好的测试程序装进 minux 文件系统的 /test/ 目录。
 *
 * 这是【库】不是工具：它没有 main，由 host/build.c 调用，和镜像构建在同一次
 * miDiskOpen 里完成 —— 分开成两个程序各自开镜像会互相覆盖。
 *
 * 前置条件: 先跑过 test/Makefile（顶层 `make` 会自动做），
 *           产物在 test/bin/<名字>.bin。
 *
 * 成功 0；失败 -1（建目录失败 / 测试程序文件不存在 / 写扇区失败）。
 */
int miTestBuildInstall(void);
