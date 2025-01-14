import os
import argparse

def delete_mod_files(source_dir):
    """
    删除源文件目录中以 `mod.c` 结尾的文件。
    """
    for root, _, files in os.walk(source_dir):
        for file in files:
            if file.endswith("mod.c"):
                file_path = os.path.join(root, file)
                try:
                    os.remove(file_path)
                    print(f"Deleted: {file_path}")
                except Exception as e:
                    print(f"Failed to delete {file_path}: {e}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="删除源文件目录中以 mod.c 结尾的文件")
    parser.add_argument('--source_dir', type=str, required=True, help='源文件目录路径')
    args = parser.parse_args()

    # 删除文件
    delete_mod_files(args.source_dir)
