import re
import argparse
import os
import shutil

# 设置命令行参数
parser = argparse.ArgumentParser(description="处理以用户指定开头和结尾的字符串。")
parser.add_argument('--input', type=str, default='input.txt', help='输入文件路径')
parser.add_argument('--output', type=str, default='output.txt', help='输出文件路径')
parser.add_argument('--tempdir', type=str, default='temp', help='临时目录路径')
parser.add_argument('--start', type=str, default='drivers', help='匹配的起始字符串')
parser.add_argument('--end', type=str, default='.o', help='匹配的结束字符串')
args = parser.parse_args()

# 读取文本文件内容
with open(args.input, 'r') as file:
    text = file.read()

# 动态生成正则表达式：匹配以用户指定开头和结尾的字符串
pattern = rf'{re.escape(args.start)}.*?{re.escape(args.end)}$'

# 使用 re.findall 提取所有符合条件的字符串
matches = re.findall(pattern, text, flags=re.MULTILINE)

# 按照字符顺序排序
matches_sorted = sorted(matches)

# 将用户指定的结束字符串替换为 .c
replacement_end = args.end.replace('.o', '.c') if args.end == '.o' else args.end
matches_replaced = [match.replace(args.end, replacement_end) for match in matches_sorted]

# 将结果写入到新的文件
with open(args.output, 'w') as file:
    file.write("\n".join(matches_replaced))

print(f"处理完成，结果已保存到 {args.output} 文件中。")

# 创建临时目录
if not os.path.exists(args.tempdir):
    os.makedirs(args.tempdir)

# 查找本地目录下所有匹配的文件并复制到临时目录
for match in matches_replaced:
    if os.path.exists(match):
        relative_path = os.path.relpath(match, start=args.start)
        dest_path = os.path.join(args.tempdir, relative_path)
        dest_dir = os.path.dirname(dest_path)
        if not os.path.exists(dest_dir):
            os.makedirs(dest_dir)
        shutil.copy(match, dest_path)
        print(f"已复制: {match} -> {dest_path}")
    else:
        print(f"未找到文件: {match}")

print(f"文件复制完成，临时目录路径为: {args.tempdir}")

