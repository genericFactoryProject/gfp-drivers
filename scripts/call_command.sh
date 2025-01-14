#!/bin/bash

# 显示用法
usage() {
  echo "Usage: $0 -d <directory> -c <command>"
  echo "  -d  指定工作目录"
  echo "  -c  要执行的命令"
  exit 1
}

# 解析命令行参数
while getopts "d:c:" opt; do
  case ${opt} in
    d )
      work_dir=$OPTARG
      ;;
    c )
      command_to_run=$OPTARG
      ;;
    * )
      usage
      ;;
  esac
done

# 检查参数是否为空
if [ -z "$work_dir" ] || [ -z "$command_to_run" ]; then
  usage
fi

# 检查工作目录是否存在
if [ ! -d "$work_dir" ]; then
  echo "错误: 工作目录 $work_dir 不存在"
  exit 1
fi

# 切换到工作目录并执行命令
echo "切换到工作目录: $work_dir"
cd "$work_dir" || exit 1

echo "执行命令: $command_to_run"
eval "$command_to_run"

echo "命令执行完成"