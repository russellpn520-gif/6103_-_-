# 分布式航班信息系统（修订版 revised_v2）

SC6103 课程项目：基于 UDP 的分布式航班信息系统（C++）。支持航班查询、座位预订、回调监控，以及 at-least-once / at-most-once 两种调用语义的对比实验。

> 本文件夹是对仓库根目录原版的**独立修订版**，根目录下的原有文件没有任何改动。和原版相比改了什么、为什么改，见 [CHANGES.md](CHANGES.md)。
> 下面的所有命令都在本文件夹内运行：`cd revised_v2`。

## 演示（三台电脑）

- 演示操作手册：[docs/SC6103_演示操作手册.pdf](docs/SC6103_演示操作手册.pdf)，每一步写明谁操作、做什么、应该看到什么。
- 启动菜单：Windows 双击 `demo.bat`，macOS 双击 `demo.command`，按数字选择：1–4 服务器（主流程与各实验配置），5 网页客户端，6–8 C++ 客户端。
- 分工：电脑 1 服务器（终端），电脑 2 C++ 客户端（终端，监控与语义实验），电脑 3 网页客户端（订票）。

## 目录

- [功能特性](#功能特性)
- [系统要求](#系统要求)
- [快速开始](#快速开始)
- [详细使用说明](#详细使用说明)
- [调用语义说明](#调用语义说明)
- [消息格式](#消息格式)
- [测试与实验](#测试与实验)
- [跨机器演示](#跨机器演示)
- [常见问题](#常见问题)
- [项目结构](#项目结构)

---

## 功能特性

### 服务

| # | 服务 | 幂等性 | 说明 |
|---|------|--------|------|
| 1 | 按出发地/目的地查询航班 | 幂等 | 返回所有匹配的航班号；无匹配时返回错误 |
| 2 | 查询航班详情 | 幂等 | 出发时间、票价、剩余座位；航班不存在时返回错误 |
| 3 | 预订座位 | **非幂等** | 每执行一次扣一次座位；航班不存在、座位数非正、座位不足分别返回对应错误 |
| 4 | 监控座位变化（回调） | — | 在监控时长内，任何客户端预订/取消后，服务器把新座位数回调给所有监控者 |
| 5 | 查询剩余座位数（自定义） | 幂等 | 只读操作，执行多少次结果都一样 |
| 6 | 取消预订（自定义） | **非幂等** | 每执行一次退回一次座位；不能退回超过已预订的座位数 |

### 分布式特性

- **自行设计的消息格式与编组**：整数用 `htonl/ntohl`，字符串带长度前缀，浮点数按 IEEE-754 位模式传输，不使用任何 RMI/RPC/序列化库
- **三种调用语义**：maybe（仅客户端，作对照）、at-least-once、at-most-once，由启动参数选择
- **重复请求过滤**：按 **（客户端 IP, 端口, 请求 ID）** 识别重复请求，服务器保存回复历史
- **消息丢失模拟**：客户端模拟请求丢失，服务器模拟回复丢失，可设随机种子以复现实验
- **回调**：从服务器主 socket 发出；同一客户端重复注册只续期，不会收到重复回调
- **跨平台**：macOS、Linux、Windows（Winsock）

---

## 系统要求

| 平台 | 编译器 | 构建工具 |
|------|--------|----------|
| macOS | Xcode Command Line Tools（clang） | `make` |
| Linux | g++（支持 C++17） | `make` |
| Windows | MinGW-w64 g++（如 MSYS2 的 `mingw-w64-ucrt-x86_64-gcc`） | `mingw32-make` 或 MSYS2 的 `make` |

单元测试另需 Google Test（可选）。

---

## 快速开始

### 1. 编译

```bash
cd revised_v2
make            # Windows (MSYS2): mingw32-make
```

生成 `server` 和 `client`（Windows 上为 `server.exe` 和 `client.exe`）。

### 2. 启动服务器

```bash
./server --port=8080 --semantics=at-most-once
```

### 3. 启动客户端

```bash
./client --server=127.0.0.1 --port=8080 --semantics=at-most-once
```

```
========== Flight Information System ==========
1. Query flights by source and destination
2. Get flight information
3. Reserve seats
4. Get seat count
5. Cancel reservation
6. Monitor seat availability
0. Exit
===============================================
Enter your choice (0-6):
```

选 `1`，输入 `Beijing`、`Shanghai`，应返回 1001、1002、1003 三个航班。输入 `0` 退出客户端，`Ctrl+C` 停止服务器。

---

## 详细使用说明

### 服务器参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--port=<port>` | 监听的 UDP 端口 | 8080 |
| `--semantics=<s>` | `at-most-once`（过滤重复请求）或 `at-least-once`（每次都执行） | at-most-once |
| `--loss-rate=<p>` | 每个**回复**被丢弃的概率（0.0-1.0） | 0 |
| `--seed=<n>` | 丢失模拟的随机种子，相同种子可复现实验 | 当前时间 |
| `--log-file=<file>` | 日志同时追加到文件 | 仅控制台 |

### 客户端参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--server=<ip>` | 服务器 IPv4 地址 | 127.0.0.1 |
| `--port=<port>` | 服务器端口 | 8080 |
| `--semantics=<s>` | `at-most-once`、`at-least-once`（两者都超时重传）或 `maybe`（不重传） | at-most-once |
| `--timeout-ms=<ms>` | 等待回复的超时时间，超时后重传 | 2000 |
| `--retries=<n>` | 第一次发送之后最多重传几次 | 5 |
| `--loss-rate=<p>` | 每个**请求**被丢弃的概率（0.0-1.0） | 0 |
| `--seed=<n>` | 丢失模拟的随机种子 | 当前时间 |
| `--log-file=<file>` | 日志同时追加到文件 | 仅控制台 |

服务器会打印收到的每个请求（来源地址、请求 ID、参数）和产生的回复（状态与内容），重复请求和模拟丢包也会单独标出。

---

## 调用语义说明

按课程（remote invocation 一章）的定义：

| 语义 | 客户端超时重传 | 服务器过滤重复请求 | 重复请求的处理 | 适用 |
|------|----------------|--------------------|----------------|------|
| maybe | 否 | — | — | 只作对照 |
| at-least-once | 是 | 否 | **重新执行** | 仅幂等操作 |
| at-most-once | 是 | 是 | 返回历史中保存的回复，**不重新执行** | 所有操作 |

- 客户端在 at-least-once 和 at-most-once 下行为相同（都会用**同一个请求 ID** 重传），区别由服务器决定，所以**两端请用同一个 `--semantics`**。
- 请求 ID 只在一个客户端内唯一（每个客户端都从 #1 开始），服务器用（客户端 IP, 端口, 请求 ID）识别重复请求。
- 回复历史保留 60 秒，长于客户端的整个重传窗口（默认 2 s × 6 次）。

```bash
# at-most-once（默认）
./server --semantics=at-most-once
./client --semantics=at-most-once

# at-least-once：回复丢失时，非幂等操作会被重复执行
./server --semantics=at-least-once --loss-rate=0.5
./client --semantics=at-least-once
```

---

## 消息格式

所有整数为 32 位网络字节序。每条消息以 12 字节头开始：

```
+--------------+--------------+----------------+-----------------------+
| message_type |  request_id  | message_length |  body (按消息类型)     |
|   uint32     |    uint32    | uint32（含头） |                       |
+--------------+--------------+----------------+-----------------------+
```

字符串编码为 `uint32 长度 + 字节`，时间编码为 5 个 int32（年、月、日、时、分），票价为 IEEE-754 float 的位模式。每种消息的完整格式见 [include/common/message_serializer.h](include/common/message_serializer.h)。解组时会检查消息类型和长度字段，格式错误的包会被丢弃并记录日志。

---

## 测试与实验

### 1. 功能测试（手动）

先启动 `./server`，再启动 `./client`，在客户端中依次测试：

| 服务 | 菜单输入 | 预期结果 |
|------|----------|----------|
| 1 | `1` → `Beijing` → `Shanghai` | 3 个航班 |
| 2 | `2` → `1001` | 航班详情（120 座） |
| 3 | `3` → `1001` → `2` | 预订成功，剩余 118 |
| 3（错误） | `3` → `9999` → `1` | Flight not found |
| 5 | `4` → `1001` | 118 |
| 6 | `5` → `1001` → `1` | 取消成功，座位数 119 |
| 4 | 另开一个客户端 `6` → `1001` → `60`，再在第一个客户端预订 | 监控端收到 `[Callback]` |

### 2. 调用语义实验（自动）

```bash
make
./tools/test_semantics.sh
```

服务器以 0.5 的概率丢弃回复，客户端超时后重传。三个实验：

1. **非幂等操作**：20 次预订（每次 1 座），比较三种语义实际扣掉的座位数
2. **幂等操作**：at-least-once 下 20 次查询座位数，结果始终正确
3. **演示场景**：一个客户端监控、另一个客户端预订，两者的第一个请求都是 #1

示例输出（seed 6103）：

```
  semantics      successes  errors  retransmissions  executions  seats taken
  maybe                  9      11                0          20           20
  at-least-once         20       0               15          35           35
  at-most-once          20       0               15          20           20

  [PASS] maybe: client was told 11 reservation(s) failed, yet 20 seats were taken for 9 successes
  [PASS] at-least-once gives a WRONG result: 35 seats taken for 20 one-seat reservations (35 executions)
  [PASS] at-most-once is correct: each reservation executed once, 20 seats taken for 20 successful reservations
```

参数可用环境变量调整，例如 `N=50 LOSS=0.3 SEED=1 ./tools/test_semantics.sh`。每次运行的服务器和客户端日志保存在 `experiment_logs/`，可直接用于报告。

### 3. 单元测试（需要 Google Test）

```bash
brew install googletest     # macOS；Ubuntu: sudo apt-get install libgtest-dev
make test                   # 编译并运行 tests_run
```

> 注意：`tests/` 下的测试源码目前不在仓库中（之前被 `.gitignore` 忽略），需要先由有这些文件的同学提交。

---

## 跨机器演示

演示要求服务器和两个客户端分别运行在不同电脑上（一个监控，一个预订）。

1. 在服务器电脑上查 IP：macOS/Linux 用 `ifconfig` 或 `ip addr`，Windows 用 `ipconfig`
2. 服务器：`./server --port=8080 --semantics=at-most-once`
3. 监控客户端：`./client --server=<服务器IP> --port=8080`，选 `6`，输入航班号和监控时长
4. 预订客户端：`./client --server=<服务器IP> --port=8080`，选 `3` 预订同一航班，监控端会打印 `[Callback] ...`
5. 演示语义差异：服务器和客户端都加 `--semantics=at-least-once`，服务器加 `--loss-rate=0.5`，多次预订后查询座位数，扣掉的座位比预订的多；换成 `at-most-once` 再做一次，结果正确

**防火墙**：Windows 第一次运行 `server.exe`/`client.exe` 时会弹出防火墙提示，请允许“专用网络”；macOS 可能询问是否允许接收传入连接，请选允许。所有电脑需连在同一网络（校园网有客户端隔离时可用手机热点）。

---

## 常见问题

**Q: 客户端一直提示 `No reply from server`？**
检查服务器是否在运行、`--server` 的 IP 和 `--port` 是否正确、防火墙是否放行 UDP 端口。

**Q: 为什么 at-least-once 下座位数不对？**
这正是实验要展示的现象：回复丢失后客户端重传，服务器不过滤重复请求，于是同一个预订被执行了多次。改用 at-most-once 即可。

**Q: 丢失率设了但看不出效果？**
丢失是随机的。提高丢失率（如 0.5）、增加操作次数，或固定 `--seed` 复现同一组丢包。

**Q: 编译报错 `make: command not found`？**
macOS: `xcode-select --install`；Ubuntu: `sudo apt-get install build-essential`；Windows: 安装 MSYS2 后执行 `pacman -S mingw-w64-ucrt-x86_64-gcc make`。

---

## 项目结构

```
revised_v2/
├── CHANGES.md                 # 与根目录原版相比的修改说明
├── include/
│   ├── common/                # 公共模块：消息类型、编组、UDP socket、平台适配、日志
│   ├── client/                # 客户端：FlightClient（请求/重传）、ClientUI（菜单）
│   └── server/                # 服务器：FlightServer、FlightManager、MonitorManager、RequestHistory
├── src/                       # 对应的实现
├── tools/
│   ├── test_semantics.sh      # 调用语义对比实验
│   ├── build.sh / clean.sh    # CMake 构建 / 清理
│   ├── run_tests.sh           # 运行单元测试
│   └── install_gtest.sh       # 安装 Google Test（macOS）
├── Makefile
└── CMakeLists.txt
```

## 网页客户端（可选扩展）

`web/` 里有一个 **Python 写的第二个客户端**（对应作业的可选加分项：客户端和服务器用不同语言）：`bridge.py` 用标准库自己做编组、超时重传和三种调用语义，作为 UDP 客户端连接本 C++ 服务器，并给本机浏览器提供一个订票网站风格的页面（每个标签页就是一个独立的 UDP 客户端，页面上能看到每个数据报的字节和重传过程）。用法、演示步骤和自测见 [web/README.md](web/README.md)：

```bash
cd web && python3 bridge.py --server=<服务器IP> --port=8080 --open
```

## 技术文档

- [修改说明（本修订版）](CHANGES.md)
- [网页客户端说明](web/README.md)
- 原版文档（仓库根目录，未改动，其中调用语义的描述已过时，以本文件夹为准）：
  [需求规格说明书](../docs/SPEC_Requirements.md)、[系统架构设计](../docs/SPEC_Architecture.md)、[开发日志](../docs/DEV_Log.md)
