#!/bin/bash

# 检查是否提供了文件名
if [ $# -eq 0 ]; then
    echo "用法: $0 <dot文件名> [输出格式]"
    echo "示例: $0 example dot"
    echo "示例: $0 example svg"
    exit 1
fi

# 获取输入参数
DOT_FILE="$1.dot"
OUTPUT_FORMAT="${2:-png}"  # 默认为png格式
OUTPUT_FILE="$1.$OUTPUT_FORMAT"

# 检查文件是否存在
if [ ! -f "$DOT_FILE" ]; then
    echo "错误: 文件 $DOT_FILE 不存在"
    exit 1
fi

# 执行转换
echo "正在将 $DOT_FILE 转换为 $OUTPUT_FILE"
dot -T$OUTPUT_FORMAT "$DOT_FILE" -o "$OUTPUT_FILE"

# 检查是否成功
if [ $? -eq 0 ]; then
    echo "✓ 转换成功: $OUTPUT_FILE"
else
    echo "✗ 转换失败"
    exit 1
fi