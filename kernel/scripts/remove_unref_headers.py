import os
import re
import argparse


def extract_includes(file_path):
    """
    提取源文件中引用的头文件路径，包括注释的头文件。
    """
    includes = set()
    # 匹配普通引用和注释引用
    include_pattern = re.compile(r'(?:\/\/\s*)?#include\s+[<"](.+?)[>"]')

    with open(file_path, 'r', encoding='utf-8') as file:
        for line in file:
            match = include_pattern.search(line)
            if match:
                includes.add(match.group(1))

    return includes


def collect_all_referenced_headers(source_dir, header_dir):
    """
    收集所有被引用的头文件，包括嵌套引用。
    """
    referenced_headers = set()
    to_process = set()

    # 初步解析源文件中的引用
    for root, _, files in os.walk(source_dir):
        for file in files:
            if file.endswith(('.c', '.h')):  # 处理源文件和头文件
                file_path = os.path.join(root, file)
                includes = extract_includes(file_path)
                for include in includes:
                    abs_path = os.path.join(header_dir, include)
                    if os.path.exists(abs_path):
                        to_process.add(abs_path)

    # 递归解析头文件中的引用
    while to_process:
        current_header = to_process.pop()
        if current_header not in referenced_headers:
            referenced_headers.add(current_header)
            includes = extract_includes(current_header)
            for include in includes:
                abs_path = os.path.join(header_dir, include)
                if os.path.exists(abs_path) and abs_path not in referenced_headers:
                    to_process.add(abs_path)

    return referenced_headers


def delete_unreferenced_headers(header_dir, referenced_headers):
    """
    删除头文件目录中未被引用的头文件。
    """
    for root, _, files in os.walk(header_dir):
        for file in files:
            if file.endswith(".h"):
                file_path = os.path.join(root, file)
                if file_path not in referenced_headers:
                    print(f"删除未被引用的头文件: {file_path}")
                    os.remove(file_path)


def main(source_dir, header_dir):
    print("解析源文件引用...")
    referenced_headers = collect_all_referenced_headers(source_dir, header_dir)
    print("解析完成，开始删除未被引用的头文件...")
    delete_unreferenced_headers(header_dir, referenced_headers)
    print("清理完成。")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="删除头文件目录中未被引用的头文件")
    parser.add_argument('--source-dir', type=str, required=True, help='源文件目录路径')
    parser.add_argument('--header-dir', type=str, required=True, help='头文件目录路径')
    args = parser.parse_args()

    main(args.source_dir, args.header_dir)
