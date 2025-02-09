#!/bin/bash

# 第一次使用

LINUX_DIR=$1
WORKSPACE_DIR=$2
BOARD_NAME=fsl
FILTER_BOARD_NAME=fsl-tmp

echo "删除多余的mod.c文件"
python3 delete_mod_files.py --source_dir $BOARD_NAME/drivers
echo "按照指定文件过滤生成的BSP"
python3 filter_directories.py --input_file $WORKSPACE_DIR/kernel/scripts/board/$BOARD_NAME/dir_list.txt --source_dir $BOARD_NAME/drivers --output_dir $FILTER_BOARD_NAME/drivers
python3 delete_mod_files.py --source_dir $FILTER_BOARD_NAME/drivers

# echo "删除BSP目录"
# rm -rf $BOARD_NAME

echo "分类源文件目录"
python3 categorize_files.py --source_dir $FILTER_BOARD_NAME/drivers --common_dir $FILTER_BOARD_NAME/common --spec_dir $FILTER_BOARD_NAME/spec

python3 find_miss_directories.py --txt_file $WORKSPACE_DIR/kernel/scripts/board/$BOARD_NAME/dir_list.txt --source_dir $FILTER_BOARD_NAME/common --output_file missing_directories.txt

echo "生成头文件-不包含arch目录的头文件"
python3 board_header.py -c $FILTER_BOARD_NAME/common -i $LINUX_DIR/include -t $FILTER_BOARD_NAME/common/include
# python3 board_header.py -c $FILTER_BOARD_NAME/spec -i $LINUX_DIR/include -t $FILTER_BOARD_NAME/spec/include

echo "拷贝相对路径的头文件"
python3 copy_includes.py --source_dir $FILTER_BOARD_NAME/common --search_dir $LINUX_DIR/drivers
# python3 copy_includes.py --source_dir $FILTER_BOARD_NAME/spec --search_dir $LINUX_DIR/drivers

echo "删除重复的头文件"
python3 delete_matching_files.py -s $FILTER_BOARD_NAME/common/include/ -d $WORKSPACE_DIR/kernel/arch/arm64/include/asm $WORKSPACE_DIR/kernel/include $WORKSPACE_DIR/lib/arch/arm64/include $WORKSPACE_DIR/lib/include
#cpython3 delete_matching_files.py -s $FILTER_BOARD_NAME/spec/include/ -d $WORKSPACE_DIR/kernel/arch/arm64/include/asm $WORKSPACE_DIR/kernel/include $WORKSPACE_DIR/lib/arch/arm64/include $WORKSPACE_DIR/lib/include

echo "删除临时目录"

mv $FILTER_BOARD_NAME/common $WORKSPACE_DIR/kernel/src/driver
mv $FILTER_BOARD_NAME/spec $WORKSPACE_DIR/bsp/fsl
mv missing_directories.txt $WORKSPACE_DIR/kernel/src/driver/common
rm -rf $FILTER_BOARD_NAME
rm -rf $WORKSPACE_DIR/bsp/fsl

echo "本次过滤应该会遗漏很多驱动，需要反复修正"