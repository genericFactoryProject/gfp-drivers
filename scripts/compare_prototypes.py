import os
import re
import argparse

# 提取头文件中的函数原型
def extract_function_prototypes(header_dirs):
    prototypes = {}
    prototype_pattern = re.compile(r'\b[\w\s\*]+?\b(\w+)\s*\([^)]*\)\s*;')  # 匹配函数原型

    for header_dir in header_dirs:
        for root, dirs, files in os.walk(header_dir):
            for file in files:
                if file.endswith(".h"):
                    header_path = os.path.join(root, file)
                    with open(header_path, 'r') as f:
                        content = f.read()
                        matches = prototype_pattern.findall(content)
                        for match in matches:
                            prototypes[match] = header_path
    return prototypes

# 提取源文件中的函数实现
def extract_function_definitions(source_dirs):
    implementations = set()
    definition_pattern = re.compile(r'\b[\w\s\*]+?\b(\w+)\s*\([^)]*\)\s*{')  # 匹配函数定义

    for source_dir in source_dirs:
        for root, dirs, files in os.walk(source_dir):
            for file in files:
                if file.endswith(".c"):
                    source_path = os.path.join(root, file)
                    with open(source_path, 'r') as f:
                        content = f.read()
                        matches = definition_pattern.findall(content)
                        implementations.update(matches)
    return implementations

# 对比函数原型和实现
def compare_prototypes_and_definitions(prototypes, implementations):
    missing_implementations = {}

    for function_name, header_path in prototypes.items():
        if function_name not in implementations:
            if header_path not in missing_implementations:
                missing_implementations[header_path] = []
            missing_implementations[header_path].append(function_name)

    return missing_implementations

# 按文件名排序并写入文件
def write_results_to_file_sorted(missing_implementations, output_file):
    sorted_results = sorted(missing_implementations.items(), key=lambda x: x[0])  # 按头文件路径排序

    with open(output_file, 'w') as f:
        if sorted_results:
            f.write(f"发现 {len(sorted_results)} 个未实现的函数原型文件：\n")
            for header_path, functions in sorted_results:
                for function_name in functions:
                    f.write(f"文件: {header_path}, 函数: {function_name}\n")
        else:
            f.write("所有函数原型均已实现。\n")
    print(f"结果已写入文件: {output_file}")

# 输出未实现的头文件统计
def write_unimplemented_headers_summary(missing_implementations, summary_file):
    unique_headers = set(missing_implementations.keys())

    with open(summary_file, 'w') as f:
        f.write(f"发现 {len(unique_headers)} 个未实现的头文件：\n")
        for header in sorted(unique_headers):
            f.write(f"{header}\n")
    print(f"未实现头文件统计已写入文件: {summary_file}")

# 主函数
def main(header_dirs, source_dirs, output_file, summary_file):
    print("正在提取头文件中的函数原型...")
    prototypes = extract_function_prototypes(header_dirs)
    print(f"找到 {len(prototypes)} 个函数原型。")

    print("正在提取源文件中的函数实现...")
    implementations = extract_function_definitions(source_dirs)
    print(f"找到 {len(implementations)} 个函数实现。")

    print("正在对比函数原型和实现...")
    missing_implementations = compare_prototypes_and_definitions(prototypes, implementations)

    # 写入详细结果
    write_results_to_file_sorted(missing_implementations, output_file)

    # 写入未实现头文件统计
    write_unimplemented_headers_summary(missing_implementations, summary_file)

# 命令行参数解析
def parse_args():
    parser = argparse.ArgumentParser(description="对比头文件中的函数原型和源文件中的函数实现")
    parser.add_argument('-hd', '--header_dirs', nargs='+', required=True, help="一个或多个头文件目录路径")
    parser.add_argument('-sd', '--source_dirs', nargs='+', required=True, help="一个或多个源文件目录路径")
    parser.add_argument('-o', '--output_file', default="results.txt", help="输出详细结果文件路径 (默认: results.txt)")
    parser.add_argument('-s', '--summary_file', default="summary.txt", help="输出未实现头文件统计文件路径 (默认: summary.txt)")
    return parser.parse_args()

# 脚本入口
if __name__ == "__main__":
    args = parse_args()  # 解析命令行参数
    main(args.header_dirs, args.source_dirs, args.output_file, args.summary_file)

