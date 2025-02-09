import os
import argparse

def find_missing_directories(txt_file, source_dir, output_file):
    """
    查找 txt 文件记录的目录名，在源目录中查找，如果没有出现，记录到输出文件。
    """
    # 读取 txt 文件中的目录名
    with open(txt_file, 'r') as f:
        directories = [line.strip() for line in f if line.strip()]

    # 获取源目录中存在的目录
    existing_directories = set()
    for root, dirs, _ in os.walk(source_dir):
        for d in dirs:
            relative_path = os.path.relpath(os.path.join(root, d), start=source_dir)
            existing_directories.add(relative_path)

    # 找出不存在的目录
    missing_directories = [d for d in directories if d not in existing_directories]

    # 将结果写入输出文件
    with open(output_file, 'w') as f:
        f.write("\n".join(missing_directories))

    print(f"查找完成，缺失目录已保存到 {output_file}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="查找源目录中缺失的目录名")
    parser.add_argument('--txt_file', type=str, required=True, help='记录目录名的 txt 文件路径')
    parser.add_argument('--source_dir', type=str, required=True, help='源目录路径')
    parser.add_argument('--output_file', type=str, default='missing_directories.txt', help='输出缺失目录的 txt 文件路径')
    args = parser.parse_args()

    find_missing_directories(args.txt_file, args.source_dir, args.output_file)
