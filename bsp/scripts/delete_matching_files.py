import os
import argparse

def delete_matching_files(source_dirs, dest_dirs):
    # 遍历每个源目录
    for source_dir in source_dirs:
        # 遍历源目录中的所有文件
        for root, _, files in os.walk(source_dir):
            for file in files:
                # 获取文件的相对路径
                relative_path = os.path.relpath(os.path.join(root, file), source_dir)

                # 在每个目的目录中查找相同的文件
                for dest_dir in dest_dirs:
                    dest_file_path = os.path.join(dest_dir, relative_path)

                    # 如果目的目录中存在这个文件，删除源目录中的文件
                    if os.path.exists(dest_file_path):
                        source_file_path = os.path.join(root, file)
                        print(f"Deleting {source_file_path} as it exists in {dest_file_path}")
                        os.remove(source_file_path)
                        break  # 一旦找到匹配文件，就停止进一步检查目的目录

def main():
    # 创建命令行解析器
    parser = argparse.ArgumentParser(description="Delete source files that match in destination directories")

    # 添加源目录参数（可以接受多个目录）
    parser.add_argument(
        '-s', '--source',
        type=str,
        nargs='+',
        required=True,
        help="Source directory or directories to check for matching files"
    )

    # 添加目的目录参数（可以接受多个目录）
    parser.add_argument(
        '-d', '--destination',
        type=str,
        nargs='+',
        required=True,
        help="Destination directory or directories to check for matching files"
    )

    # 解析命令行参数
    args = parser.parse_args()

    # 调用删除匹配文件的函数
    delete_matching_files(args.source, args.destination)

if __name__ == "__main__":
    main()
