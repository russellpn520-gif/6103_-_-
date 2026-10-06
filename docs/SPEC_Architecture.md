# 分布式航班信息系统 - 系统架构设计

**版本：** 1.0  
**创建日期：** 2026-09-28  
**编程语言：** C++  
**开发环境：** macOS  

> **2026-10-06 更正**：本文档是实现前的设计稿，其中的类接口（如 `setAtLeastOnce`）已过时。调用语义、去重键、
> 回调发送方式和跨平台 socket 层已修改，以 README 和源代码为准（变更见 [DEV_Log.md](DEV_Log.md) 2026-10-06 一节）。

---

## 1. 系统架构概览

### 1.1 整体架构

系统采用经典的**客户端-服务器架构**，基于 UDP 协议通信：

```
┌─────────────────────────────────────────────────────────┐
│                        系统架构                          │
├─────────────────────────────────────────────────────────┤
│                                                          │
│  ┌──────────┐         UDP          ┌──────────┐        │
│  │ 客户端 1 │ ◄──────────────────► │          │        │
│  └──────────┘                      │          │        │
│                                    │  服务器  │        │
│  ┌──────────┐         UDP          │          │        │
│  │ 客户端 2 │ ◄──────────────────► │          │        │
│  └──────────┘                      │          │        │
│                                    │          │        │
│  ┌──────────┐         UDP          │          │        │
│  │ 客户端 N │ ◄──────────────────► │          │        │
│  └──────────┘                      └──────────┘        │
│                                                          │
└─────────────────────────────────────────────────────────┘
```

### 1.2 架构特点

- **无连接通信**：UDP 协议，不需要建立连接
- **消息驱动**：客户端发送请求，服务器返回响应
- **回调机制**：服务器可以主动向客户端发送消息
- **单线程服务器**：假设请求在时间上分离，不需要多线程

---

## 2. 模块划分

系统分为三个主要模块：

### 2.1 公共模块（Common）
提供客户端和服务器共享的基础功能：
- 消息编组/解组
- 数据结构定义
- 协议常量
- 工具函数

### 2.2 客户端模块（Client）
提供用户界面和远程调用功能：
- 用户界面（命令行）
- 请求发送
- 响应接收
- 调用语义实现（重试、去重）
- 回调接收

### 2.3 服务器模块（Server）
提供业务逻辑和数据管理：
- 请求处理
- 航班管理
- 监控管理
- 历史记录（去重用）
- 回调发送

---

## 3. 类设计

### 3.1 公共模块类图

```
┌─────────────────────────────────────────────────────────┐
│                    公共模块 (Common)                      │
├─────────────────────────────────────────────────────────┤
│                                                          │
│  ┌──────────────────┐                                   │
│  │ MessageSerializer │  消息编组/解组                     │
│  ├──────────────────┤                                   │
│  │ + marshalQueryFlightRequest(...)                     │
│  │ + unmarshalQueryFlightRequest(...)                   │
│  │ + marshalFlightInfoResponse(...)                     │
│  │ + unmarshalFlightInfoResponse(...)                   │
│  │ ... (其他 marshal/unmarshal 方法)                    │
│  └──────────────────┘                                   │
│                                                          │
│  ┌──────────────────┐                                   │
│  │   UdpSocket      │  UDP Socket 封装                   │
│  ├──────────────────┤                                   │
│  │ - sockfd: int    │                                   │
│  │ + bind(port)     │                                   │
│  │ + sendto(...)    │                                   │
│  │ + recvfrom(...)  │                                   │
│  │ + close()        │                                   │
│  └──────────────────┘                                   │
│                                                          │
│  ┌──────────────────┐                                   │
│  │     Logger       │  日志系统                          │
│  ├──────────────────┤                                   │
│  │ + info(msg)      │                                   │
│  │ + error(msg)     │                                   │
│  │ + debug(msg)     │                                   │
│  └──────────────────┘                                   │
│                                                          │
└─────────────────────────────────────────────────────────┘
```

### 3.2 客户端模块类图

```
┌─────────────────────────────────────────────────────────┐
│                   客户端模块 (Client)                     │
├─────────────────────────────────────────────────────────┤
│                                                          │
│  ┌──────────────────┐                                   │
│  │  FlightClient    │  客户端主类                        │
│  ├──────────────────┤                                   │
│  │ - serverAddr     │                                   │
│  │ - serverPort     │                                   │
│  │ - socket         │                                   │
│  │ - requestId      │                                   │
│  │ - atLeastOnce    │                                   │
│  │ - lossRate       │                                   │
│  ├──────────────────┤                                   │
│  │ + queryFlight()  │                                   │
│  │ + getFlightInfo()│                                   │
│  │ + reserveSeats() │                                   │
│  │ + registerMonitor│                                   │
│  │ + getSeatCount() │                                   │
│  │ + cancelReserv() │                                   │
│  │ - sendRequest()  │                                   │
│  │ - sendWithRetry()│                                   │
│  │ - receiveCallback│                                   │
│  └──────────────────┘                                   │
│                                                          │
│  ┌──────────────────┐                                   │
│  │   ClientUI       │  用户界面                          │
│  ├──────────────────┤                                   │
│  │ + showMenu()     │                                   │
│  │ + getInput()     │                                   │
│  │ + displayResult()│                                   │
│  │ + run()          │  主循环                            │
│  └──────────────────┘                                   │
│                                                          │
└─────────────────────────────────────────────────────────┘
```

### 3.3 服务器模块类图

```
┌─────────────────────────────────────────────────────────┐
│                   服务器模块 (Server)                     │
├─────────────────────────────────────────────────────────┤
│                                                          │
│  ┌──────────────────┐                                   │
│  │  FlightServer    │  服务器主类                        │
│  ├──────────────────┤                                   │
│  │ - port           │                                   │
│  │ - socket         │                                   │
│  │ - flightManager  │                                   │
│  │ - monitorManager │                                   │
│  │ - history        │                                   │
│  │ - atLeastOnce    │                                   │
│  │ - lossRate       │                                   │
│  ├──────────────────┤                                   │
│  │ + start()        │  启动服务器                        │
│  │ + stop()         │  停止服务器                        │
│  │ + addFlight()    │  添加测试航班                      │
│  │ - handleRequest()│  处理请求                          │
│  │ - notifyMonitor()│  通知监控客户端                    │
│  └──────────────────┘                                   │
│                                                          │
│  ┌──────────────────┐                                   │
│  │ FlightManager    │  航班管理器                        │
│  ├──────────────────┤                                   │
│  │ - flights: map   │                                   │
│  ├──────────────────┤                                   │
│  │ + queryFlight()  │                                   │
│  │ + getFlightInfo()│                                   │
│  │ + reserveSeats() │                                   │
│  │ + getSeatCount() │                                   │
│  │ + cancelReserv() │                                   │
│  │ + addFlight()    │                                   │
│  └──────────────────┘                                   │
│                                                          │
│  ┌──────────────────┐                                   │
│  │ MonitorManager   │  监控管理器                        │
│  ├──────────────────┤                                   │
│  │ - monitors: list │                                   │
│  ├──────────────────┤                                   │
│  │ + register()     │  注册监控                          │
│  │ + notify()       │  通知监控客户端                    │
│  │ + cleanup()      │  清理过期监控                      │
│  └──────────────────┘                                   │
│                                                          │
│  ┌──────────────────┐                                   │
│  │ RequestHistory   │  请求历史记录                      │
│  ├──────────────────┤                                   │
│  │ - history: map   │                                   │
│  ├──────────────────┤                                   │
│  │ + addEntry()     │  添加记录                          │
│  │ + findEntry()    │  查找记录                          │
│  │ + cleanup()      │  清理旧记录                        │
│  └──────────────────┘                                   │
│                                                          │
└─────────────────────────────────────────────────────────┘
```

---

## 4. 数据流设计

### 4.1 查询航班数据流

```
客户端                              服务器
  │                                   │
  │  1. 用户输入查询条件              │
  │                                   │
  │  2. 编组请求消息                  │
  │     [航班号, 出发地, 目的地]      │
  │                                   │
  │  ─────── UDP 请求 ─────────────► │
  │                                   │
  │                          3. 解组请求
  │                                   │
  │                          4. 查询航班数据
  │                                   │
  │                          5. 编组响应消息
  │                             [航班列表]
  │                                   │
  │  ◄────── UDP 响应 ────────────── │
  │                                   │
  │  6. 解组响应                      │
  │                                   │
  │  7. 显示结果                      │
  │                                   │
```

### 4.2 预订座位数据流

```
客户端                              服务器
  │                                   │
  │  1. 用户输入预订信息              │
  │                                   │
  │  2. 编组请求（含 request_id）     │
  │                                   │
  │  ─────── UDP 请求 ─────────────► │
  │                                   │
  │                          3. 检查历史记录
  │                             是否重复请求？
  │                                   │
  │                          4a. 是重复 → 返回之前结果
  │                                   │
  │                          4b. 新请求 → 执行预订
  │                             - 检查座位数
  │                             - 更新座位数
  │                             - 记录到历史
  │                                   │
  │                          5. 编组响应
  │                                   │
  │  ◄────── UDP 响应 ────────────── │
  │                                   │
  │  6. 显示结果                      │
  │                                   │
```

### 4.3 监控回调数据流

```
客户端 A (监控)          服务器              客户端 B (预订)
  │                        │                        │
  │  1. 注册监控           │                        │
  │  ───────────────────► │                        │
  │                        │                        │
  │                        │  2. 记录监控信息       │
  │                        │     (航班号, 客户端地址)│
  │                        │                        │
  │  ◄─────────────────── │                        │
  │  3. 注册成功           │                        │
  │                        │                        │
  │  4. 等待回调...        │                        │
  │                        │                        │
  │                        │             5. 预订座位 │
  │                        │ ◄───────────────────── │
  │                        │                        │
  │                        │  6. 更新座位数         │
  │                        │                        │
  │                        │  7. 检查监控列表       │
  │                        │                        │
  │  ◄─────────────────── │                        │
  │  8. 收到回调           │                        │
  │  (座位更新通知)        │                        │
  │                        │                        │
  │  9. 显示更新           │                        │
  │                        │                        │
```

---

## 5. 项目目录结构

```
flight_system/
│
├── docs/                              # 文档目录
│   ├── SPEC_Requirements.md           # 需求规格说明书
│   ├── SPEC_Architecture.md           # 架构设计文档（本文件）
│   ├── DEV_Log.md                     # 开发日志
│   └── TEST_Report.md                 # 测试报告
│
├── include/                           # 头文件目录
│   ├── common/                        # 公共模块头文件
│   │   ├── message_types.h            # 消息类型定义
│   │   ├── data_structures.h          # 数据结构定义
│   │   ├── message_serializer.h       # 消息编组/解组
│   │   ├── udp_socket.h               # UDP Socket 封装
│   │   ├── logger.h                   # 日志系统
│   │   └── error_codes.h              # 错误码定义
│   │
│   ├── client/                        # 客户端头文件
│   │   ├── flight_client.h            # 客户端主类
│   │   └── client_ui.h                # 用户界面
│   │
│   └── server/                        # 服务器头文件
│       ├── flight_server.h            # 服务器主类
│       ├── flight_manager.h           # 航班管理器
│       ├── monitor_manager.h          # 监控管理器
│       └── request_history.h          # 请求历史记录
│
├── src/                               # 源代码目录
│   ├── common/                        # 公共模块实现
│   │   ├── message_serializer.cpp
│   │   ├── udp_socket.cpp
│   │   └── logger.cpp
│   │
│   ├── client/                        # 客户端实现
│   │   ├── flight_client.cpp
│   │   ├── client_ui.cpp
│   │   └── client_main.cpp            # 客户端入口
│   │
│   └── server/                        # 服务器实现
│       ├── flight_server.cpp
│       ├── flight_manager.cpp
│       ├── monitor_manager.cpp
│       ├── request_history.cpp
│       └── server_main.cpp            # 服务器入口
│
├── tests/                             # 测试代码目录
│   ├── test_message_serializer.cpp    # 消息编组测试
│   ├── test_flight_manager.cpp        # 航班管理测试
│   ├── test_monitor_manager.cpp       # 监控管理测试
│   ├── test_request_history.cpp       # 历史记录测试
│   ├── test_flight_client.cpp         # 客户端测试
│   ├── test_flight_server.cpp         # 服务器测试
│   ├── test_integration.cpp           # 集成测试
│   └── test_main.cpp                  # 测试入口
│
├── tools/                             # 工具脚本
│   ├── build.sh                       # 构建脚本
│   ├── run_tests.sh                   # 运行测试脚本
│   └── clean.sh                       # 清理脚本
│
├── data/                              # 数据文件
│   └── sample_flights.txt             # 测试航班数据
│
├── CMakeLists.txt                     # CMake 构建文件
├── Makefile                           # Make 构建文件（备用）
└── README.md                          # 项目说明
```

---

## 6. 核心类详细设计

### 6.1 MessageSerializer（消息编组器）

**职责：** 将数据结构序列化为字节流（编组），将字节流反序列化为数据结构（解组）

**关键方法：**

```cpp
class MessageSerializer {
public:
    // 编组查询航班请求
    static void marshalQueryFlightRequest(
        uint32_t request_id,
        const std::string& source,
        const std::string& destination,
        std::vector<uint8_t>& buffer
    );
    
    // 解组查询航班请求
    static void unmarshalQueryFlightRequest(
        const std::vector<uint8_t>& buffer,
        uint32_t& request_id,
        std::string& source,
        std::string& destination
    );
    
    // 编组查询航班响应
    static void marshalQueryFlightResponse(
        uint32_t request_id,
        int32_t status_code,
        const std::vector<int32_t>& flight_ids,
        std::vector<uint8_t>& buffer
    );
    
    // ... 其他 marshal/unmarshal 方法
    
private:
    // 辅助方法：写入整数（大端序）
    static void writeUint32(std::vector<uint8_t>& buffer, uint32_t value);
    
    // 辅助方法：读取整数（大端序）
    static uint32_t readUint32(const std::vector<uint8_t>& buffer, size_t& offset);
    
    // 辅助方法：写入字符串
    static void writeString(std::vector<uint8_t>& buffer, const std::string& str);
    
    // 辅助方法：读取字符串
    static std::string readString(const std::vector<uint8_t>& buffer, size_t& offset);
};
```

### 6.2 UdpSocket（UDP Socket 封装）

**职责：** 封装 UDP Socket 操作，提供简洁的接口

**关键方法：**

```cpp
class UdpSocket {
public:
    UdpSocket();
    ~UdpSocket();
    
    // 绑定端口（服务器用）
    bool bind(uint16_t port);
    
    // 发送数据报
    ssize_t sendTo(
        const void* data,
        size_t length,
        const struct sockaddr_in& dest_addr
    );
    
    // 接收数据报
    ssize_t recvFrom(
        void* buffer,
        size_t buffer_size,
        struct sockaddr_in& src_addr,
        int timeout_seconds = 0  // 0 表示不超时
    );
    
    // 设置超时
    void setTimeout(int seconds);
    
    // 关闭 socket
    void close();
    
private:
    int sockfd_;
};
```

### 6.3 FlightManager（航班管理器）

**职责：** 管理航班数据的增删改查

**关键方法：**

```cpp
class FlightManager {
public:
    // 查询航班（按出发地和目的地）
    std::vector<int32_t> queryFlight(
        const std::string& source,
        const std::string& destination
    );
    
    // 获取航班详情
    bool getFlightInfo(int32_t flight_id, Flight& flight);
    
    // 预订座位
    bool reserveSeats(int32_t flight_id, int32_t seat_count, int32_t& remaining);
    
    // 获取座位数
    bool getSeatCount(int32_t flight_id, int32_t& count);
    
    // 取消预订
    bool cancelReservation(int32_t flight_id, int32_t seat_count, int32_t& new_count);
    
    // 添加航班（测试用）
    void addFlight(const Flight& flight);
    
private:
    std::map<int32_t, Flight> flights_;  // 航班号 -> 航班信息
    std::mutex mutex_;  // 线程安全（虽然单线程，但预留）
};
```

### 6.4 MonitorManager（监控管理器）

**职责：** 管理客户端的监控注册和回调通知

**关键方法：**

```cpp
class MonitorManager {
public:
    // 注册监控
    void registerMonitor(
        int32_t flight_id,
        int32_t interval_seconds,
        const struct sockaddr_in& client_addr
    );
    
    // 通知监控客户端
    void notifyMonitors(int32_t flight_id, int32_t new_seat_count);
    
    // 清理过期监控
    void cleanupExpired();
    
private:
    struct MonitorEntry {
        int32_t flight_id;
        struct sockaddr_in client_addr;
        time_t expire_time;
    };
    
    std::vector<MonitorEntry> monitors_;
    UdpSocket socket_;  // 用于发送回调
};
```

### 6.5 RequestHistory（请求历史记录）

**职责：** 记录已处理的请求，用于去重

**关键方法：**

```cpp
class RequestHistory {
public:
    // 添加记录
    void addEntry(
        uint32_t request_id,
        const std::vector<uint8_t>& response
    );
    
    // 查找记录
    bool findEntry(
        uint32_t request_id,
        std::vector<uint8_t>& response
    );
    
    // 清理旧记录（超过 60 秒的）
    void cleanup();
    
private:
    struct HistoryEntry {
        uint32_t request_id;
        std::vector<uint8_t> response;
        time_t timestamp;
    };
    
    std::map<uint32_t, HistoryEntry> history_;
};
```

### 6.6 FlightClient（客户端主类）

**职责：** 封装客户端逻辑，提供服务调用接口

**关键方法：**

```cpp
class FlightClient {
public:
    FlightClient(const std::string& server_ip, uint16_t server_port);
    ~FlightClient();
    
    // 服务 1：查询航班
    int32_t queryFlight(
        const std::string& source,
        const std::string& destination,
        std::vector<int32_t>& flight_ids
    );
    
    // 服务 2：查询航班详情
    int32_t getFlightInfo(int32_t flight_id, Flight& flight);
    
    // 服务 3：预订座位
    int32_t reserveSeats(
        int32_t flight_id,
        int32_t seat_count,
        int32_t& remaining_seats
    );
    
    // 服务 4：注册监控（阻塞）
    int32_t registerMonitor(
        int32_t flight_id,
        int32_t interval_seconds,
        std::function<void(int32_t, int32_t, int32_t)> callback
    );
    
    // 服务 5：查询座位数
    int32_t getSeatCount(int32_t flight_id, int32_t& count);
    
    // 服务 6：取消预订
    int32_t cancelReservation(
        int32_t flight_id,
        int32_t seat_count,
        int32_t& new_count
    );
    
    // 设置调用语义
    void setAtLeastOnce(bool enable);
    
    // 设置丢失率
    void setLossRate(double rate);
    
private:
    // 发送请求并接收响应
    int32_t sendRequest(
        const std::vector<uint8_t>& request,
        std::vector<uint8_t>& response
    );
    
    // 带重试的发送
    int32_t sendRequestWithRetry(
        const std::vector<uint8_t>& request,
        std::vector<uint8_t>& response,
        int max_retries = 3
    );
    
    // 生成请求 ID
    uint32_t generateRequestId();
    
    // 模拟消息丢失
    bool shouldSimulateLoss();
    
    std::string server_ip_;
    uint16_t server_port_;
    UdpSocket socket_;
    uint32_t request_id_counter_;
    bool at_least_once_;
    double loss_rate_;
};
```

### 6.7 FlightServer（服务器主类）

**职责：** 接收请求，调用业务逻辑，返回响应

**关键方法：**

```cpp
class FlightServer {
public:
    FlightServer(uint16_t port);
    ~FlightServer();
    
    // 启动服务器（阻塞）
    void start();
    
    // 停止服务器
    void stop();
    
    // 添加测试航班
    void addFlight(const Flight& flight);
    
    // 设置调用语义
    void setAtLeastOnce(bool enable);
    
    // 设置丢失率
    void setLossRate(double rate);
    
private:
    // 处理请求的主循环
    void requestLoop();
    
    // 处理单个请求
    void handleRequest(
        const std::vector<uint8_t>& request,
        struct sockaddr_in& client_addr
    );
    
    // 各服务处理器
    void handleQueryFlight(
        uint32_t request_id,
        const std::vector<uint8_t>& request,
        struct sockaddr_in& client_addr
    );
    
    void handleGetFlightInfo(...);
    void handleReserveSeats(...);
    void handleRegisterMonitor(...);
    void handleGetSeatCount(...);
    void handleCancelReservation(...);
    
    // 发送响应
    void sendResponse(
        const std::vector<uint8_t>& response,
        const struct sockaddr_in& client_addr
    );
    
    // 模拟消息丢失
    bool shouldSimulateLoss();
    
    uint16_t port_;
    UdpSocket socket_;
    FlightManager flight_manager_;
    MonitorManager monitor_manager_;
    RequestHistory history_;
    bool running_;
    bool at_least_once_;
    double loss_rate_;
};
```

---

## 7. 依赖关系

### 7.1 模块依赖

```
┌─────────────────────────────────────────┐
│         客户端 (Client)                  │
│  ┌─────────────────────────────────┐   │
│  │ FlightClient                    │   │
│  │ - 依赖 MessageSerializer       │   │
│  │ - 依赖 UdpSocket               │   │
│  │ - 依赖 Logger                  │   │
│  └─────────────────────────────────┘   │
│  ┌─────────────────────────────────┐   │
│  │ ClientUI                        │   │
│  │ - 依赖 FlightClient            │   │
│  └─────────────────────────────────┘   │
└─────────────────────────────────────────┘

┌─────────────────────────────────────────┐
│         服务器 (Server)                  │
│  ┌─────────────────────────────────┐   │
│  │ FlightServer                    │   │
│  │ - 依赖 FlightManager           │   │
│  │ - 依赖 MonitorManager          │   │
│  │ - 依赖 RequestHistory          │   │
│  │ - 依赖 MessageSerializer       │   │
│  │ - 依赖 UdpSocket               │   │
│  └─────────────────────────────────┘   │
└─────────────────────────────────────────┘

┌─────────────────────────────────────────┐
│         公共模块 (Common)                │
│  - MessageSerializer                   │
│  - UdpSocket                           │
│  - Logger                              │
│  - DataStructures                      │
└─────────────────────────────────────────┘
```

### 7.2 外部依赖

- **C++11 或更高版本**
- **POSIX Socket API**（macOS 原生支持）
- **Google Test**（测试框架，可选）
- **CMake**（构建工具，可选）

---

## 8. 构建策略

### 8.1 使用 CMake

```cmake
cmake_minimum_required(VERSION 3.10)
project(FlightSystem)

set(CMAKE_CXX_STANDARD 11)

# 包含目录
include_directories(include)

# 公共库
add_library(common
    src/common/message_serializer.cpp
    src/common/udp_socket.cpp
    src/common/logger.cpp
)

# 客户端
add_executable(client
    src/client/flight_client.cpp
    src/client/client_ui.cpp
    src/client/client_main.cpp
)
target_link_libraries(client common)

# 服务器
add_executable(server
    src/server/flight_server.cpp
    src/server/flight_manager.cpp
    src/server/monitor_manager.cpp
    src/server/request_history.cpp
    src/server/server_main.cpp
)
target_link_libraries(server common)

# 测试
add_executable(tests
    tests/test_main.cpp
    tests/test_message_serializer.cpp
    tests/test_flight_manager.cpp
    # ... 其他测试文件
)
target_link_libraries(tests common gtest)
```

### 8.2 构建命令

```bash
# 配置
cmake -B build

# 编译
cmake --build build

# 运行测试
./build/tests

# 运行服务器
./build/server --port=8080

# 运行客户端
./build/client --server=127.0.0.1 --port=8080
```

---

## 9. 关键设计决策

### 9.1 为什么使用单线程服务器？

**决策：** 服务器使用单线程处理请求

**理由：**
- 题目假设请求在时间上分离
- 简化实现，避免并发问题
- 足够演示核心概念

**替代方案：** 多线程服务器（增加复杂度，但不是必须）

### 9.2 为什么客户端在监控时阻塞？

**决策：** 客户端在监控期间阻塞，不能执行其他操作

**理由：**
- 题目明确允许这种简化
- 避免引入多线程
- 足够演示回调机制

**替代方案：** 使用多线程（客户端一个线程接收回调，主线程处理用户输入）

### 9.3 为什么使用 map 存储航班？

**决策：** 使用 `std::map<int32_t, Flight>` 存储航班

**理由：**
- 航班号是唯一标识
- map 提供 O(log n) 的查找性能
- 自动按航班号排序

**替代方案：** `std::unordered_map`（O(1) 查找，但无序）

---

## 10. 扩展性考虑

### 10.1 未来可能的扩展

1. **持久化存储**：将航班数据保存到文件
2. **多线程服务器**：支持并发请求处理
3. **认证机制**：用户登录和权限控制
4. **图形界面**：使用 Qt 或其他 GUI 框架
5. **分布式部署**：多个服务器协同工作

### 10.2 当前设计的扩展性

- **模块化设计**：各模块独立，易于替换
- **接口抽象**：使用接口定义，便于扩展
- **配置化**：参数可配置（端口、丢失率等）

---

## 11. 验收标准

### 11.1 架构验收

- [ ] 模块划分清晰
- [ ] 类职责单一
- [ ] 依赖关系合理
- [ ] 接口设计完整

### 11.2 实现验收

- [ ] 所有类正确实现
- [ ] 单元测试覆盖率 > 80%
- [ ] 集成测试通过
- [ ] 性能满足要求

---

**文档版本历史：**
- v1.0 (2026-09-28): 初始版本
