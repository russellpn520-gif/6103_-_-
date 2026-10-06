#!/bin/bash

# 运行单元测试脚本

set -e

echo "=========================================="
echo "单元测试运行器"
echo "=========================================="
echo ""

# 检查是否已编译
if [ ! -f "./tests_run" ]; then
    echo "❌ 测试程序未找到"
    echo ""
    echo "请按以下步骤操作："
    echo ""
    echo "1️⃣  安装 Google Test（如果未安装）："
    echo "   macOS: brew install googletest"
    echo "   或运行: ./tools/install_gtest.sh"
    echo ""
    echo "2️⃣  编译测试程序："
    echo "   make test"
    echo ""
    echo "3️⃣  再次运行此脚本："
    echo "   ./tools/run_tests.sh"
    echo ""
    echo "详细说明请查看 README.md 的"测试指南"部分"
    echo ""
    exit 1
fi

echo "✅ 找到测试程序"
echo ""
echo "开始运行测试..."
echo ""

# 运行测试
./tests_run

# 显示测试结果
EXIT_CODE=$?

echo ""
echo "=========================================="
if [ $EXIT_CODE -eq 0 ]; then
    echo "✅ 所有测试通过！"
else
    echo "❌ 部分测试失败"
    echo "退出码: $EXIT_CODE"
fi
echo "=========================================="

exit $EXIT_CODE
