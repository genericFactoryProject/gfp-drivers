import os
import argparse
import re

def process_source_files(source_dir, header_dir):
    """
    遍历源文件目录，删除注释掉的头文件引用。
    """
    for root, _, files in os.walk(source_dir):
        for file in files:
            if file.endswith(".c"):  # 只处理 .c 文件
                source_path = os.path.join(root, file)
                process_file(source_path, header_dir)

def process_file(source_path, header_dir):
    """
    处理单个源文件，删除注释掉的头文件。
    """
    with open(source_path, 'r') as file:
        lines = file.readlines()

    modified = False
    new_lines = []

    for line in lines:
        # 匹配注释掉的头文件行
        match = re.match(r'^\s*//+\s*#include\s+<(.+)>', line)
        if match:
            header_name = match.group(1)
            header_path = os.path.join(header_dir, header_name)

            # 检查头文件是否存在
            if os.path.exists(header_path):
                print(f"删除注释的头文件: <{header_name}> in {source_path}")
                modified = True
                continue  # 删除该行

        new_lines.append(line)

    # 如果文件有修改，则写回文件
    if modified:
        with open(source_path, 'w') as file:
            file.writelines(new_lines)

if __name__ == "__main__":
    # 设置命令行参数
    parser = argparse.ArgumentParser(description="删除源文件中注释的头文件。")
    parser.add_argument('--source-dir', type=str, required=True, help='源文件目录')
    parser.add_argument('--header-dir', type=str, required=True, help='头文件目录')
    args = parser.parse_args()

    # 调用处理函数
    process_source_files(args.source_dir, args.header_dir)
