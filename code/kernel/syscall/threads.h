//
// Created by koddnty on 2026/11/1.
// 进程管理函数
//
#pragma once

#include "kernel/base.h"

pid_t fork(void);

// 执行某个进程（暂时设置为加载后直接执行）
pid_t exec(const char* pwd);

// 获取当前进程pid
pid_t get_pid();