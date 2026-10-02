cmake_minimum_required(VERSION 3.20)
project(minux C CXX ASM)

# 头文件搜索根目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/code)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/host)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/code/minFs)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/code/tools)

# 只为索引，不真正编译
add_executable(minux_index
    code/minFs/fsTree.c
    code/tools/cstring.c
        code/kernel/lib/kprintf.c
    code/kernel/main.c
    host/fsSys.c
    host/build.c
    test/print.c
)

