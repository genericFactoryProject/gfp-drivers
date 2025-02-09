import os
import re
import shutil
import argparse

# Step 1: 获取所有 .c 文件
def find_c_files(directory):
    c_files = []
    for root, dirs, files in os.walk(directory):
        for file in files:
            if file.endswith('.c'):  # 只查找 .c 文件
                c_files.append(os.path.join(root, file))
    return c_files

# Step 2: 从 .c 文件中提取 #include 语句
def extract_includes(c_file):
    include_pattern = re.compile(r'#include\s+[<"](.*)[>"]')  # 匹配 #include 语句
    includes = []

    with open(c_file, 'r') as file:
        content = file.read()
        includes = include_pattern.findall(content)

    return includes

# Step 3: 根据 #include 语句查找头文件
def find_and_copy_include_files(c_file, include_dir, temp_dir):
    includes = extract_includes(c_file)

    for include in includes:
        # 在工作目录中查找头文件
        include_path = os.path.join(include_dir, include)

        if os.path.exists(include_path):
            # 获取头文件的目录结构
            rel_dir = os.path.dirname(include)
            dest_dir = os.path.join(temp_dir, rel_dir)

            # 创建目标目录
            os.makedirs(dest_dir, exist_ok=True)

            # 复制文件到临时目录
            dest_file = os.path.join(dest_dir, os.path.basename(include))
            shutil.copy2(include_path, dest_file)
            print(f"已复制头文件: {include_path} 到 {dest_file}")
        else:
            print(f"未找到头文件: {include_path}")

# 主函数：执行上述步骤
def main(c_files_dir, include_dir, temp_dir):
    # 获取所有 .c 文件
    c_files = find_c_files(c_files_dir)

    if not c_files:
        print("没有找到任何 .c 文件。")
        return

    # 对每个 .c 文件执行处理
    for c_file in c_files:
        print(f"正在处理文件: {c_file}")
        find_and_copy_include_files(c_file, include_dir, temp_dir)

# 设置命令行参数
def parse_args():
    parser = argparse.ArgumentParser(description="根据 .c 文件中的 #include 查找并复制头文件")

    # 定义命令行选项
    parser.add_argument('-c', '--c_files_dir', required=True, help="包含 .c 文件的目录")
    parser.add_argument('-i', '--include_dir', required=True, help="头文件所在的目录")
    parser.add_argument('-t', '--temp_dir', required=True, help="临时目录路径，用于保存头文件")

    return parser.parse_args()

# 执行程序
if __name__ == "__main__":
    args = parse_args()  # 获取命令行参数

    # 执行主函数
    main(args.c_files_dir, args.include_dir, args.temp_dir)
