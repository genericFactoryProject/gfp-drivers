import os
import shutil
import argparse

def parse_relative_directories(input_file):
    """
    从输入文件中解析相对路径
    """
    directories = set()
    with open(input_file, 'r') as file:
        for line in file:
            line = line.strip()
            if line:  # 跳过空行
                directories.add(line)
    return directories

def filter_source_directory(source_dir, output_dir, relative_directories):
    """
    基于相对路径过滤源文件目录
    """
    if os.path.exists(output_dir):
        shutil.rmtree(output_dir)  # 清理旧的输出目录
    os.makedirs(output_dir)

    for relative_dir in relative_directories:
        source_path = os.path.join(source_dir, relative_dir)
        if os.path.exists(source_path) and os.path.isdir(source_path):
            # 保留目录及其内容
            dest_path = os.path.join(output_dir, relative_dir)
            shutil.copytree(source_path, dest_path, dirs_exist_ok=True)
            print(f"Copied directory: {source_path} -> {dest_path}")
        else:
            print(f"Warning: Directory not found - {source_path}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="过滤源文件目录，仅保留指定的相对路径及其子目录")
    parser.add_argument('--input_file', type=str, required=True, help='包含相对路径信息的输入文本文件')
    parser.add_argument('--source_dir', type=str, required=True, help='源文件目录')
    parser.add_argument('--output_dir', type=str, required=True, help='输出文件目录')
    args = parser.parse_args()

    # 解析相对路径
    relative_directories = parse_relative_directories(args.input_file)

    # 过滤源文件目录
    filter_source_directory(args.source_dir, args.output_dir, relative_directories)
