#!/bin/bash
# 构建脚本

set -e

echo "Building Flight System..."

# 创建构建目录
mkdir -p build
cd build

# 配置
echo "Configuring..."
cmake ..

# 编译
echo "Compiling..."
cmake --build .

echo "Build complete!"
echo ""
echo "Executables:"
echo "  Server: build/server"
echo "  Client: build/client"
if [ -f tests ]; then
    echo "  Tests:  build/tests"
fi
