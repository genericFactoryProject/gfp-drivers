import os
import re
import argparse
import subprocess
import shutil

def run_cmake_and_compile(build_dir):
    """运行 CMake 和编译，返回编译输出和返回码"""
    try:
        result = subprocess.run(
            ["cmake", "."],
            cwd=build_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        if result.returncode != 0:
            return result.stderr, result.returncode

        result = subprocess.run(
            ["make"],
            cwd=build_dir,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        return result.stderr, result.returncode
    except Exception as e:
        return str(e), 1

def find_missing_file(error_message):
    """从错误消息中提取缺失的文件路径"""
    match = re.search(r"fatal error: (.+?): No such file or directory", error_message)
    if match:
        return match.group(1).strip()
    return None

def search_file_in_directories(filename, search_dirs):
    """组合缺失文件名与搜索目录生成绝对路径，并检查文件是否存在"""
    for search_dir in search_dirs:
        candidate_path = os.path.join(search_dir, filename)
        if os.path.exists(candidate_path):
            return candidate_path
    return None

def copy_file_to_dest_dir(file_path, dest_dir, filename):
    """将文件复制到目标目录中，保持原始目录结构"""
    dest_path = os.path.join(dest_dir, filename)
    dest_dir_path = os.path.dirname(dest_path)

    if not os.path.exists(dest_dir_path):
        os.makedirs(dest_dir_path)

    shutil.copy(file_path, dest_path)
    print(f"Copied: {file_path} -> {dest_path}")

if __name__ == "__main__":
    # 配置命令行参数
    parser = argparse.ArgumentParser(description="CMake 编译自动处理缺失文件")
    parser.add_argument("--build-dir", type=str, required=True, help="本地构建目录路径")
    parser.add_argument("--dest-dir", type=str, required=True, help="缺失文件复制到的目标目录路径")
    parser.add_argument("--search-dirs", type=str, nargs='+', required=True, help="缺失文件搜索目录路径（支持多个）")
    parser.add_argument("--max-retries", type=int, default=5, help="最大重试次数，默认值为 5")
    args = parser.parse_args()

    build_dir = args.build_dir
    dest_dir = args.dest_dir
    search_dirs = args.search_dirs
    max_retries = args.max_retries

    # 创建构建目录
    os.makedirs(build_dir, exist_ok=True)

    for attempt in range(max_retries):
        print(f"Attempt {attempt + 1}/{max_retries}: Compiling...")

        # 执行 CMake 和编译
        output, returncode = run_cmake_and_compile(build_dir)

        # 如果成功退出，则停止
        if returncode == 0:
            print("Compilation successful!")
            break

        # 查找缺失的文件
        missing_file = find_missing_file(output)
        if missing_file:
            print(f"Missing file detected: {missing_file}")

            # 在指定目录中搜索缺失文件的绝对路径
            found_path = search_file_in_directories(missing_file, search_dirs)
            if found_path:
                print(f"Found missing file at: {found_path}")

                # 复制文件到目标目录
                copy_file_to_dest_dir(found_path, dest_dir, missing_file)
            else:
                print(f"Error: Missing file {missing_file} not found in any of the specified directories.")
                break
        else:
            print("Error: No 'No such file or directory' error found.")
            break
    else:
        print("Max retries reached. Compilation failed.")
