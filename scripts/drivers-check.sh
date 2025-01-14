#!/bin/bash

LINUX_DIR=$1
WORKSPACE_DIR=$2
BOARD_NAME=fsl

echo "$LINUX_DIR $WORKSPACE_DIR"

echo "删除多余的mod.c文件"
python3 delete_mod_files.py --source_dir generated/drivers

echo "按照指定文件过滤生成的BSP"
python3 filter_directories.py --input_file board/$BOARD_NAME/dir_list.txt --source_dir generated/drivers --output_dir tmp/drivers

rm -rf generated
mv tmp generated

echo "删除generated目录下重名的文件"
python3 delete_matching_files.py -s generated/include/ -d $WORKSPACE_DIR/kernel/arch/arm64/include/asm $WORKSPACE_DIR/kernel/include $WORKSPACE_DIR/lib/arch/arm64/include $WORKSPACE_DIR/lib/include
python3 delete_matching_files.py -s generated/drivers/base/ -d $WORKSPACE_DIR/kernel/src/driver/base
python3 delete_matching_files.py -s generated/drivers/of -d $WORKSPACE_DIR/kernel/src/driver/base/of

echo "拷贝相对路径的头文件"
python3 copy_includes.py --source_dir generated/drivers/ --search_dir $LINUX_DIR/drivers

# echo "增加删除common的文件"
python3 delete_matching_files.py -s generated/drivers -d $WORKSPACE_DIR/kernel/src/driver/common
python3 delete_matching_files.py -s generated/include -d $WORKSPACE_DIR/kernel/src/driver/common

mv generated/include generated/drivers
mv generated/drivers ../../bsp/scripts/fsl

rm -rf generated

# echo "重复构建测试"
# python3 build_with_retry.py --build-dir generated --search-dirs $LINUX_DIR/include $LINUX_DIR/arch/arm64/include $LINUX_DIR/arch/arm64/include/generated --dest-dir=generated/include --max-retries 100000
