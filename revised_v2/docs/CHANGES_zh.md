# 修订版（revised_v2）修改说明

本文件夹是仓库根目录原版代码的**独立修订版**。根目录下的所有原有文件（源码、文档、脚本）都保持原样，没有被覆盖或修改。
修订是对照课程项目要求（Course.Project.for2026a.pdf）和课堂讲解做的，2026-10-06。

## 一、主要问题与修正

### 1. 调用语义标反了（最重要）

按课程定义（remote invocation 一章）：

| 语义 | 客户端超时重传 | 服务器过滤重复请求 | 重复请求的处理 |
|------|----------------|--------------------|----------------|
| maybe | 否 | — | — |
| at-least-once | 是 | 否 | 重新执行 |
| at-most-once | 是 | 是 | 返回历史中保存的回复，不重新执行 |

原版的默认模式（不重传、不去重）其实是 *maybe*，原版的 `--at-least-once`（客户端重传 + 服务器去重）其实是 *at-most-once*。原版 `docs/DEV_Log.md` 里“At-Least-Once 适合非幂等操作、需要服务器去重”的结论也是反的，**不要写进报告**。

修订版：
- 参数改为 `--semantics=at-least-once|at-most-once`（客户端另有 `maybe` 作对照），两端用同一个值，默认 at-most-once。
- 两种语义下客户端都会用**同一个请求 ID** 超时重传；服务器只在 at-most-once 下过滤重复请求、返回历史回复，在 at-least-once 下每次都重新执行。

### 2. 去重只按 request_id

原版每个客户端的请求 ID 都从 1 开始，服务器只按 request_id 去重。在 demo 场景中，监控客户端的注册请求 #1 和另一台电脑上预订客户端的预订请求 #1 会“撞号”：预订被当成重复请求、根本没执行，预订客户端解析错误类型的回复时崩溃。

修订版按（客户端 IP, 端口, request_id）识别重复请求，也就是老师课上说的 client identifier + request identifier。

### 3. 缺少要求的对比实验

作业要求“证明 at-least-once 对非幂等操作会出错，而 at-most-once 对所有操作都正确”。原版的 `test_semantics.sh` 没有做这个实验（第 6 项无论结果如何都算通过）。

修订版的 [tools/test_semantics.sh](tools/test_semantics.sh) 让服务器以 0.5 的概率丢弃回复，结果（seed 6103，20 次预订各 1 座）：

| 语义 | 成功 | 报错 | 重传次数 | 服务器执行次数 | 实际扣座位 |
|------|------|------|----------|----------------|------------|
| maybe | 9 | 11 | 0 | 20 | 20 |
| at-least-once | 20 | 0 | 15 | 35 | **35（错误）** |
| at-most-once | 20 | 0 | 15 | 20 | **20（正确）** |

- maybe：客户端以为 11 次失败，但服务器其实都执行了。
- at-least-once：丢失回复后重传的请求被重新执行，多扣了 15 个座位。
- at-most-once：重复请求由历史记录应答，每个预订只执行一次。
- 幂等操作（查座位数）在 at-least-once 下被重复执行 15 次，20 个回答全部正确。
- “一个客户端监控、另一个客户端预订、两者请求 ID 都是 #1”的演示场景：预订正常执行，监控端收到回调。

每次运行的服务器和客户端日志保存在 `experiment_logs/`，可直接用于报告。

## 二、其他改动

- **输入校验**：预订不存在的航班返回 “Flight not found”（原版是 “Insufficient seats”）；座位数必须为正（原版预订负数会增加座位）；取消的座位数不能超过已预订数（原版可以凭空加座位）；监控时长限制为 1-3600 秒。
- **健壮性**：服务器捕获解组异常，畸形 UDP 包不再导致服务器退出；所有解组函数检查消息类型和长度字段；客户端只接受 request id 和类型都匹配的回复，丢弃过期/重复的回复。
- **回调**：改为从服务器主 socket 发出（原版另开 socket、从随机端口发出，跨机器时容易被防火墙/NAT 拦截）；同一客户端重复注册同一航班只续期，不会收到重复回调。
- **服务器输出**：打印每个请求的来源、参数和回复内容（作业要求打印收到的请求和产生的回复），并标出重复请求是“重新执行”还是“从历史返回”。
- **客户端界面**：监控时先打印“已注册”再等待，按剩余时间等待，结束时不再报超时警告；城市名可以含空格；非数字输入会重新提示，不会让客户端退出。
- **Windows 支持**：新增 [include/common/platform.h](include/common/platform.h)（Winsock 适配），Makefile/CMake 在 Windows 上链接 ws2_32。
- **构建**：Makefile 自动跟踪头文件依赖；CMake 与 Makefile 统一为 C++17；`.gitignore` 不再忽略 `tests/` 源码目录（测试二进制名为 `tests_run`）。
- **注释**：所有源码注释改为英文；删除 `message_serializer.cpp` 里“为了节省空间只列出关键几个”的误导性占位注释。

## 三、新增 / 修改的文件（相对原版）

| 文件 | 说明 |
|------|------|
| `include/common/platform.h` | 新增：跨平台 socket 适配 |
| `include/common/cli_options.h` | 新增：命令行参数解析与范围检查 |
| `include/common/message_types.h` | 新增错误码、`Semantics` 枚举、消息类型名 |
| `include/common/message_serializer.h`, `src/common/message_serializer.cpp` | 写明完整消息格式；解组检查类型和长度 |
| `include/common/udp_socket.h`, `src/common/udp_socket.cpp` | 毫秒级超时、Windows 支持、地址转字符串 |
| `include/server/request_history.h`, `src/server/request_history.cpp` | 去重键改为（IP, 端口, request_id） |
| `include/server/monitor_manager.h`, `src/server/monitor_manager.cpp` | 用主 socket 发回调；重复注册只续期 |
| `include/server/flight_manager.h`, `src/server/flight_manager.cpp` | 输入校验、记录航班容量 |
| `include/server/flight_server.h`, `src/server/flight_server.cpp` | 语义处理、异常捕获、打印请求和回复 |
| `src/server/server_main.cpp`, `src/client/client_main.cpp` | 新参数 `--semantics`、`--timeout-ms`、`--retries`、`--seed` |
| `include/client/*`, `src/client/*` | 重传逻辑、回复匹配、监控流程、输入处理 |
| `tools/test_semantics.sh` | 重写为对比实验 |
| `Makefile`, `CMakeLists.txt`, `.gitignore`, `.gitattributes`, `README.md` | 见上 |

## 四、验证情况

- 用 zig 交叉编译了 Linux、macOS（arm64 / x86_64）、Windows 版本，项目代码在 `-Wall -Wextra -Wshadow` 下零警告。
- 在 Windows 上实际运行 `tools/test_semantics.sh`，6/6 通过；手动测试了各种错误输入、畸形 UDP 包、服务器离线、请求和回复双向丢包。
- **尚未验证**：Makefile/CMake 本身没在本机执行过（本机没有 make/cmake），请在 Mac 上跑一次 `make && ./tools/test_semantics.sh`；跨机器演示需要三台电脑实际联调一次。

## 五、需要注意

- 根目录 `tests/` 下的单元测试源码从未提交到仓库（被原版 `.gitignore` 的 `/tests` 忽略）。如果要用于修订版，`reserveSeats` / `cancelReservation` 的新校验可能需要相应调整测试用例。
- 修订版与原版的命令行参数不兼容：原版的 `--at-least-once` 在修订版中已删除，会直接报错。
