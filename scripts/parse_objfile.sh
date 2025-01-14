#!/bin/bash

# $1: Linux Component Dirname
echo "$1"
echo "清除构建产物"
make clean
rm input.txt

echo "构建进行中"
make defconfig ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
make -j64 ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- > input.txt 2>&1

echo "移除临时文件"
rm -rf temp temp_header
rm output.txt

echo "生成源文件"
python3 board_dep.py --input input.txt --output output.txt --tempdir temp --start "$1" --end ".o"

echo "生成头文件-不包含arch目录的头文件"
python3 board_header.py -c temp -i include -t temp_header
