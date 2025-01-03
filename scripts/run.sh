#!/bin/bash

echo "该脚本是特定场景使用的，需要用户自己修改，该目录其他文件是通用的，不需修改"

LINUX_ARCH=arm64
LINUX_DIR=$1
LINUX_BOARD=fsl

cp board/$LINUX_BOARD/defconfig $LINUX_DIR/arch/$LINUX_ARCH/configs
cp board_dep.py board_header.py parse_objfile.sh $LINUX_DIR
./call_command.sh -d $LINUX_DIR -c "./parse_objfile.sh drivers"
echo "由于不包含arch目录的头文件，可能在编译时出现找不到头文件的错误，这块是需要我们自己解决接口依赖问题"
mkdir generated
mv $LINUX_DIR/temp generated/drivers
mv $LINUX_DIR/temp_header generated/include
rm $LINUX_DIR/board_dep.py $LINUX_DIR/board_header.py $LINUX_DIR/parse_objfile.sh
python3 compare_prototypes.py -sd generated/drivers ../../lib/src/linux -hd generated/include
echo "result.txt中包含的所有头文件以及调用的源文件都需要重新配置，这个配置包括是否应删除drivers下源文件以及头文件；或是自己去实现源文件，补全实现"
# python3 check_function_calls.py -r results.txt -sd generated/drivers
echo "没有考虑头文件包含头文件的情况"
