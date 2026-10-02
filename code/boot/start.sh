#!/bin/bash
# repair: 这个脚本原来处于「草稿」状态，根本跑不了：
#   1) 整个文件被包在 ```bash 代码块里，第 1 行不是 shebang；
#   2) 路径指向 $BasePath/minux/boot/loader.asm，而 boot 目录早就搬到 code/boot/ 了；
#   3) 它自己 nasm/ld/objcopy/dd 一条龙，和 host/build.sh、Makefile 三份逻辑并存，
#      改一处就会漂（"改了代码但镜像没更新"就是这么来的）。
# 现在编译/装配统一交给仓库根目录的 Makefile：一份布局、一份依赖、一条命令。
# 这里只保留转发，你原来的习惯 `bash code/boot/start.sh` 仍然能用；
# 也可以直接删掉这个文件，改用 `make run` / `make image`。
set -e
exec make -C "$(dirname "$0")/../.." "$@"
