import os
import argparse
import shutil

def classify_files(source_dir, common_dir, spec_dir):
    """
    将源文件目录分为 common 和 spec 两个子目录。
    - 如果文件在第一级子目录下，且不包含 "static struct platform_driver"，放到 common。
    - 其他文件放到 spec。
    - 子目录保持原有的相对路径。
    """
    if not os.path.exists(common_dir):
        os.makedirs(common_dir)
    if not os.path.exists(spec_dir):
        os.makedirs(spec_dir)

    for root, _, files in os.walk(source_dir):
        for file in files:
            if not file.endswith(".c"):  # 只处理 .c 文件
                continue

            file_path = os.path.join(root, file)
            relative_path = os.path.relpath(file_path, start=source_dir)
            relative_parts = relative_path.split(os.sep)

            # 判断是否在第一级子目录下
            is_first_level = len(relative_parts) == 2

            with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()

            # 确定目标目录
            if is_first_level and "static struct platform_driver" not in content:
                target_dir = os.path.join(common_dir, os.path.dirname(relative_path))
            else:
                target_dir = os.path.join(spec_dir, os.path.dirname(relative_path))

            # 创建目标目录
            if not os.path.exists(target_dir):
                os.makedirs(target_dir)

            # 移动文件
            target_path = os.path.join(target_dir, file)
            shutil.move(file_path, target_path)
            print(f"Moved: {file_path} -> {target_path}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="将源文件目录分成 common 和 spec 两个子目录")
    parser.add_argument('--source_dir', type=str, required=True, help='源文件目录路径')
    parser.add_argument('--common_dir', type=str, default='common', help='common 目录路径')
    parser.add_argument('--spec_dir', type=str, default='spec', help='spec 目录路径')
    args = parser.parse_args()

    classify_files(args.source_dir, args.common_dir, args.spec_dir)
