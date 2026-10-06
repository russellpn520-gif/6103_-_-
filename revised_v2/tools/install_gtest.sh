#!/bin/bash

# Google Test 安装脚本 (macOS)

echo "=========================================="
echo "Google Test 安装脚本 (macOS)"
echo "=========================================="

# 检查是否已安装
if pkg-config --exists gtest; then
    echo "✅ Google Test 已安装"
    exit 0
fi

# 检查是否有 Homebrew
if ! command -v brew &> /dev/null; then
    echo "❌ 未找到 Homebrew"
    echo ""
    echo "请先安装 Homebrew:"
    echo "  /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
    echo ""
    echo "或者手动安装 Google Test:"
    echo "  1. 下载 Google Test: https://github.com/google/googletest"
    echo "  2. 编译安装:"
    echo "     cd googletest"
    echo "     mkdir build && cd build"
    echo "     cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local"
    echo "     make"
    echo "     sudo make install"
    exit 1
fi

echo "📦 正在安装 Google Test..."
brew install googletest

echo ""
echo "✅ Google Test 安装完成"
echo ""
echo "现在可以运行测试:"
echo "  make test"
