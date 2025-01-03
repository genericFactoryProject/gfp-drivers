import os
import re
import argparse
from concurrent.futures import ThreadPoolExecutor

# 解析 result.txt 获取未实现的函数原型
def parse_result_file(result_file):
    unimplemented_functions = {}
    pattern = re.compile(r'文件: (.*?), 函数: (\w+)')  # 匹配文件和函数名
    with open(result_file, 'r') as f:
        for line in f:
            match = pattern.search(line)
            if match:
                header_file = match.group(1)
                function_name = match.group(2)
                if header_file not in unimplemented_functions:
                    unimplemented_functions[header_file] = []
                unimplemented_functions[header_file].append(function_name)
    return unimplemented_functions

# 检查源文件中是否调用了这些未实现的函数
def check_function_calls_in_file(source_file, function_calls):
    matched_functions = []
    call_pattern = re.compile(r'\b({})\s*\('.format('|'.join(function_calls)))  # 匹配函数调用

    with open(source_file, 'r') as f:
        for line in f:
            matches = call_pattern.findall(line)
            if matches:
                matched_functions.extend(matches)

    return matched_functions

# 处理源文件目录中的每个源文件
def process_source_files(source_dir, unimplemented_functions):
    function_calls = {func: [] for funcs in unimplemented_functions.values() for func in funcs}  # 函数名到文件映射
    source_files = []

    # 获取所有源文件路径
    for root, dirs, files in os.walk(source_dir):
        for file in files:
            if file.endswith(".c"):
                source_files.append(os.path.join(root, file))

    # 使用多线程加速源文件的检查
    with ThreadPoolExecutor() as executor:
        results = executor.map(lambda file: check_function_calls_in_file(file, function_calls), source_files)

    # 收集匹配结果
    for matched_functions in results:
        for func in matched_functions:
            if func in function_calls:
                function_calls[func].append(source_file)

    return function_calls

# 写入详细检查结果到文件
def write_detailed_results(unimplemented_functions, function_calls, output_file):
    with open(output_file, 'w') as f:
        for header_file, functions in unimplemented_functions.items():
            f.write(f"头文件: {header_file}\n")
            for func in functions:
                if function_calls[func]:
                    f.write(f"  函数: {func} 被调用于以下文件中:\n")
                    for file in function_calls[func]:
                        f.write(f"    - {file}\n")
                else:
                    f.write(f"  函数: {func} 未被调用。\n")
    print(f"详细调用检查结果已写入文件: {output_file}")

# 写入未调用的函数和头文件到另一个文件
def write_uninvoked_results(unimplemented_functions, function_calls, uninvoked_file):
    with open(uninvoked_file, 'w') as f:
        for header_file, functions in unimplemented_functions.items():
            for func in functions:
                if not function_calls[func]:
                    f.write(f"头文件: {header_file}, 函数: {func}\n")
    print(f"未调用的函数原型已写入文件: {uninvoked_file}")

# 主函数
def main(result_file, source_dir, detailed_output_file, uninvoked_output_file):
    print("正在解析 results.txt...")
    unimplemented_functions = parse_result_file(result_file)
    total_functions = sum(len(funcs) for funcs in unimplemented_functions.values())
    print(f"找到 {total_functions} 个未实现的函数原型。")

    print("正在检查源文件中的函数调用...")
    function_calls = process_source_files(source_dir, unimplemented_functions)

    print("正在写入结果...")
    write_detailed_results(unimplemented_functions, function_calls, detailed_output_file)
    write_uninvoked_results(unimplemented_functions, function_calls, uninvoked_output_file)

# 命令行参数解析
def parse_args():
    parser = argparse.ArgumentParser(description="检查未实现的函数原型是否被源文件调用")
    parser.add_argument('-r', '--result_file', required=True, help="未实现函数的 result.txt 文件路径")
    parser.add_argument('-sd', '--source_dir', required=True, help="源文件目录路径")
    parser.add_argument('-o', '--detailed_output_file', default="call_results.txt", help="详细结果文件路径 (默认: call_results.txt)")
    parser.add_argument('-u', '--uninvoked_output_file', default="uninvoked_results.txt", help="未调用函数结果文件路径 (默认: uninvoked_results.txt)")
    return parser.parse_args()

# 脚本入口
if __name__ == "__main__":
    args = parse_args()  # 解析命令行参数
    main(args.result_file, args.source_dir, args.detailed_output_file, args.uninvoked_output_file)
