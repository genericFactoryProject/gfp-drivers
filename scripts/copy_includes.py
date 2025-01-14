import os
import re
import shutil
import argparse

def find_header_relative(header_name, source_file_path, search_dir, source_dir):
    """
    根据源文件的相对路径，在搜索目录中查找头文件
    """
    # 计算源文件的相对路径
    relative_path = os.path.relpath(source_file_path, source_dir)
    relative_dir = os.path.dirname(relative_path)

    # 在搜索目录中构造对应的路径
    possible_header_path = os.path.join(search_dir, relative_dir, header_name)

    # 检查是否存在该头文件
    if os.path.exists(possible_header_path):
        return possible_header_path
    return None

def process_source_files(source_dir, search_dir):
    """
    处理源文件，拷贝缺失的头文件到源文件目录
    """
    for root, _, files in os.walk(source_dir):
        for file in files:
            if file.endswith('.c'):
                source_file_path = os.path.join(root, file)

                # 解析源文件
                with open(source_file_path, 'r') as src_file:
                    content = src_file.readlines()

                # 搜索并拷贝头文件
                for line in content:
                    match = re.match(r'#include\s+"([^"]+)"', line)
                    if match:
                        header_name = match.group(1)
                        header_path = find_header_relative(
                            header_name, source_file_path, search_dir, source_dir
                        )

                        # 目标头文件路径
                        destination_header_path = os.path.join(root, header_name)

                        # 如果目标路径存在，跳过
                        if os.path.exists(destination_header_path):
                            print(f"Skipped (already exists): {destination_header_path}")
                            continue

                        # 如果找到头文件，复制到目标路径
                        if header_path:
                            # 确保目标目录存在
                            destination_dir = os.path.dirname(destination_header_path)
                            if not os.path.exists(destination_dir):
                                os.makedirs(destination_dir)
                                print(f"Created directory: {destination_dir}")

                            shutil.copy(header_path, destination_header_path)
                            print(f"Copied: {header_path} -> {destination_header_path}")
                        else:
                            print(f"Header not found: {header_name} relative to {search_dir}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="解析源文件并复制头文件")
    parser.add_argument('--source_dir', type=str, required=True, help='源文件目录，也是目标目录')
    parser.add_argument('--search_dir', type=str, required=True, help='头文件搜索目录')
    args = parser.parse_args()

    process_source_files(args.source_dir, args.search_dir)
