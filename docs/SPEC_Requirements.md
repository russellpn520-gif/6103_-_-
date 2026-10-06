# 分布式航班信息系统 - 需求规格说明书

**版本：** 1.0  
**创建日期：** 2026-09-28  
**编程语言：** C++  
**开发环境：** macOS  

> **2026-10-06 更正**：本文档是实现前的设计稿。调用语义、去重键、命令行参数和客户端/服务器接口已按课程定义修正，
> 以 README 的“调用语义说明”和源代码为准（变更见 [DEV_Log.md](DEV_Log.md) 2026-10-06 一节）。

---

## 1. 系统概述

### 1.1 项目背景
本项目是一个分布式航班信息系统，用于演示分布式系统中的进程间通信和远程调用技术。系统基于客户端-服务器架构，使用 UDP 协议进行通信。

### 1.2 系统目标
- 实现航班信息的分布式管理
- 演示 UDP Socket 编程
- 实现消息编组/解组
- 实现两种调用语义（At-Most-Once 和 At-Least-Once）
- 实现回调机制

### 1.3 系统范围
本系统包括：
- 一个服务器程序：存储和管理航班信息
- 多个客户端程序：提供用户界面，调用服务器服务
- 通信协议：定义客户端和服务器之间的消息格式

### 1.4 系统约束
- **必须使用 UDP 协议**（不能使用 TCP）
- **必须手动实现消息编组/解组**（不能使用 RMI、RPC、CORBA、Java 序列化等）
- **必须实现两种调用语义**
- **必须实现回调机制**
- 航班信息存储在内存中（不要求持久化）
- 假设请求在时间上是分离的（服务器不需要多线程处理）

---

## 2. 功能需求

### 2.1 服务 1：按出发地和目的地查询航班

**功能描述：**  
根据出发地和目的地查询所有匹配的航班。

**输入：**
- 出发地（字符串，变长）
- 目的地（字符串，变长）

**输出：**
- 成功：返回所有匹配的航班号列表（整数数组）
- 失败：返回错误信息

**前置条件：**
- 客户端已连接到服务器
- 出发地和目的地字符串不为空

**后置条件：**
- 服务器返回匹配结果或错误信息

**业务规则：**
1. 匹配规则：精确匹配出发地和目的地
2. 如果多个航班匹配，返回所有航班号
3. 如果没有匹配航班，返回错误信息 "No flights found"

**示例：**
```
输入：出发地="Beijing", 目的地="Shanghai"
输出：航班号列表 [1001, 1002, 1003]

输入：出发地="AAA", 目的地="BBB"
输出：错误 "No flights found"
```

**错误处理：**
- 出发地或目的地为空：返回错误 "Invalid input"
- 无匹配航班：返回错误 "No flights found"

---

### 2.2 服务 2：按航班号查询航班详情

**功能描述：**  
根据航班号查询航班的详细信息。

**输入：**
- 航班号（整数）

**输出：**
- 成功：返回航班详情（出发地、目的地、出发时间、票价、可用座位数）
- 失败：返回错误信息

**前置条件：**
- 客户端已连接到服务器
- 航班号大于 0

**后置条件：**
- 服务器返回航班详情或错误信息

**业务规则：**
1. 航班号必须唯一
2. 返回所有航班信息字段

**数据结构：**
```cpp
struct FlightInfo {
    int flight_id;           // 航班号
    std::string source;      // 出发地
    std::string destination; // 目的地
    struct Time {
        int year;
        int month;
        int day;
        int hour;
        int minute;
    } departure_time;        // 出发时间
    float airfare;           // 票价
    int seat_availability;   // 可用座位数
};
```

**示例：**
```
输入：航班号=1001
输出：
  出发地：Beijing
  目的地：Shanghai
  出发时间：2026-10-20 08:30
  票价：800.50
  可用座位：120
```

**错误处理：**
- 航班号不存在：返回错误 "Flight not found"

---

### 2.3 服务 3：预订座位

**功能描述：**  
为指定航班预订指定数量的座位。

**输入：**
- 航班号（整数）
- 预订座位数（整数）

**输出：**
- 成功：返回确认信息和更新后的可用座位数
- 失败：返回错误信息

**前置条件：**
- 客户端已连接到服务器
- 航班号存在
- 预订座位数大于 0

**后置条件：**
- 成功时：航班的可用座位数减少
- 失败时：航班信息不变

**业务规则：**
1. 检查航班是否存在
2. 检查可用座位数是否足够
3. 如果足够，减少可用座位数并返回成功
4. 如果不足，返回错误

**幂等性：** **非幂等**
- 多次执行会产生不同效果（每次都会减少座位数）

**示例：**
```
输入：航班号=1001, 座位数=3
输出：预订成功，剩余座位 117

输入：航班号=1001, 座位数=200
输出：错误 "Insufficient seats"
```

**错误处理：**
- 航班号不存在：返回错误 "Flight not found"
- 座位数不足：返回错误 "Insufficient seats"
- 座位数 <= 0：返回错误 "Invalid seat count"

---

### 2.4 服务 4：监控座位可用性更新（回调机制）

**功能描述：**  
客户端注册监控某个航班的座位可用性更新，在指定的监控时间内，每当该航班的座位数发生变化时，服务器通过回调通知客户端。

**输入：**
- 航班号（整数）
- 监控时间间隔（秒，整数）

**输出：**
- 注册成功：返回确认信息
- 监控期间：接收服务器的回调消息（座位更新）
- 监控到期：自动解除监控

**前置条件：**
- 客户端已连接到服务器
- 航班号存在
- 监控时间间隔大于 0

**后置条件：**
- 服务器记录客户端的监控请求
- 客户端在监控期间被阻塞，等待回调

**业务规则：**
1. 客户端提供航班号和监控时间间隔
2. 服务器记录客户端的 IP 地址和端口号
3. 在监控期间，每当该航班的座位数发生变化（通过服务 3 预订），服务器向所有监控该航班的客户端发送回调消息
4. 监控时间到期后，服务器自动移除该客户端的监控记录
5. 客户端在监控期间被阻塞，不能发送新请求
6. 多个客户端可以同时监控同一个航班

**回调消息格式：**
```cpp
struct CallbackMessage {
    int flight_id;           // 航班号
    int new_seat_count;      // 新的可用座位数
    int remaining_time;      // 剩余监控时间（秒）
};
```

**示例：**
```
客户端 A：注册监控航班 1001，监控时间 60 秒
服务器：注册成功

客户端 B：预订航班 1001 的 3 个座位
服务器：
  1. 更新座位数：120 → 117
  2. 向客户端 A 发送回调：航班 1001，座位数 117，剩余时间 58 秒

客户端 A：收到回调，显示 "航班 1001 座位更新：117"

60 秒后：
服务器：移除客户端 A 的监控记录
客户端 A：解除阻塞
```

**错误处理：**
- 航班号不存在：返回错误 "Flight not found"
- 监控时间 <= 0：返回错误 "Invalid monitor interval"

**实现要点：**
- 服务器需要维护一个监控列表
- 每次座位预订后，检查是否有客户端在监控该航班
- 如果有，向所有监控该航班的客户端发送回调
- 定期检查并清理过期的监控记录

---

### 2.5 服务 5（自定义幂等服务）：查询航班剩余座位数

**功能描述：**  
查询指定航班的当前可用座位数。

**输入：**
- 航班号（整数）

**输出：**
- 成功：返回当前可用座位数
- 失败：返回错误信息

**前置条件：**
- 客户端已连接到服务器
- 航班号存在

**后置条件：**
- 无状态改变（纯查询操作）

**业务规则：**
1. 检查航班是否存在
2. 返回当前可用座位数
3. 不改变任何状态

**幂等性：** **幂等**
- 多次执行效果完全相同
- 例如：`get_seat_count(1001)` 执行 1 次和 10 次，返回的结果都相同，且不会改变系统状态

**示例：**
```
输入：航班号=1001
输出：当前座位数 120

输入：航班号=1001（再次执行）
输出：当前座位数 120（结果相同）
```

**错误处理：**
- 航班号不存在：返回错误 "Flight not found"

---

### 2.6 服务 6（自定义非幂等服务）：取消预订

**功能描述：**  
取消指定航班的座位预订，增加可用座位数。

**输入：**
- 航班号（整数）
- 取消的座位数（整数）

**输出：**
- 成功：返回确认信息和更新后的可用座位数
- 失败：返回错误信息

**前置条件：**
- 客户端已连接到服务器
- 航班号存在
- 取消的座位数 > 0

**后置条件：**
- 航班的可用座位数增加

**业务规则：**
1. 检查航班是否存在
2. 增加可用座位数
3. 返回成功确认

**幂等性：** **非幂等**
- 多次执行会产生累积效果
- 例如：`cancel_reservation(1001, 5)` 执行 2 次，座位数会增加 10

**示例：**
```
输入：航班号=1001, 取消座位数=5
输出：取消成功，当前座位数 125

输入：航班号=1001, 取消座位数=5（再次执行）
输出：取消成功，当前座位数 130（座位数再次增加）
```

**错误处理：**
- 航班号不存在：返回错误 "Flight not found"
- 取消座位数 <= 0：返回错误 "Invalid seat count"

---

## 3. 非功能需求

### 3.1 性能要求
- 响应时间：单个请求的响应时间 < 1 秒（局域网环境）
- 并发支持：支持至少 10 个客户端同时连接
- 监控支持：支持至少 5 个客户端同时监控同一个航班

### 3.2 可靠性要求
- 消息丢失：必须能够处理 UDP 消息丢失
- 超时机制：客户端请求超时时间可配置（`--timeout-ms`，默认 2000 ms）
- 重传机制：At-Least-Once 和 At-Most-Once 下客户端都会超时重传（`--retries`，默认 5 次）；区别在服务器：At-Most-Once 按（客户端地址, 请求 ID）过滤重复请求并返回历史中的回复，At-Least-Once 不过滤、每次都重新执行

### 3.3 可测试性要求
- 消息丢失模拟：能够模拟不同概率的消息丢失（0%、10%、30%、50%）
- 日志记录：服务器和客户端都需要记录关键操作日志
- 测试数据：提供预定义的测试航班数据

### 3.4 可移植性要求
- 操作系统：支持 macOS、Linux
- 编译器：支持 GCC、Clang
- 标准：使用 C++11 或更高版本

---

## 4. 数据结构定义

### 4.1 航班数据结构

```cpp
// 时间结构
struct Time {
    int year;        // 年
    int month;       // 月
    int day;         // 日
    int hour;        // 时
    int minute;      // 分
};

// 航班信息结构
struct Flight {
    int flight_id;           // 航班号
    std::string source;      // 出发地
    std::string destination; // 目的地
    Time departure_time;     // 出发时间
    float airfare;           // 票价
    int seat_availability;   // 可用座位数
};
```

### 4.2 监控记录数据结构

```cpp
// 监控记录
struct MonitorEntry {
    int flight_id;                    // 监控的航班号
    struct sockaddr_in client_addr;   // 客户端地址
    time_t expire_time;               // 到期时间
    bool active;                      // 是否有效
};
```

### 4.3 历史记录数据结构（用于去重）

```cpp
// 历史记录条目
struct HistoryEntry {
    int request_id;      // 请求 ID
    int operation_type;  // 操作类型
    std::vector<uint8_t> reply_data;  // 响应数据
    time_t timestamp;    // 时间戳
};
```

---

## 5. 消息协议设计

### 5.1 消息类型定义

```cpp
enum MessageType {
    // 服务 1：查询航班
    MSG_QUERY_FLIGHT_REQUEST = 1,
    MSG_QUERY_FLIGHT_RESPONSE = 2,
    
    // 服务 2：查询航班详情
    MSG_GET_FLIGHT_INFO_REQUEST = 3,
    MSG_GET_FLIGHT_INFO_RESPONSE = 4,
    
    // 服务 3：预订座位
    MSG_RESERVE_SEATS_REQUEST = 5,
    MSG_RESERVE_SEATS_RESPONSE = 6,
    
    // 服务 4：监控座位更新
    MSG_REGISTER_MONITOR_REQUEST = 7,
    MSG_REGISTER_MONITOR_RESPONSE = 8,
    MSG_CALLBACK_UPDATE = 9,
    
    // 服务 5：查询剩余座位数
    MSG_GET_SEAT_COUNT_REQUEST = 10,
    MSG_GET_SEAT_COUNT_RESPONSE = 11,
    
    // 服务 6：取消预订
    MSG_CANCEL_RESERVATION_REQUEST = 12,
    MSG_CANCEL_RESERVATION_RESPONSE = 13,
};
```

### 5.2 通用消息头

```cpp
// 消息头（所有消息都包含）
struct MessageHeader {
    uint32_t message_type;    // 消息类型（4字节）
    uint32_t request_id;      // 请求 ID（4字节）
    uint32_t message_length;  // 消息总长度（4字节）
};
// 固定 12 字节
```

### 5.3 请求消息格式

#### 5.3.1 查询航班请求
```
消息类型：MSG_QUERY_FLIGHT_REQUEST (1)
格式：
  - 消息头（12字节）
  - 出发地长度（4字节）
  - 出发地（变长）
  - 目的地长度（4字节）
  - 目的地（变长）
```

#### 5.3.2 查询航班详情请求
```
消息类型：MSG_GET_FLIGHT_INFO_REQUEST (3)
格式：
  - 消息头（12字节）
  - 航班号（4字节）
```

#### 5.3.3 预订座位请求
```
消息类型：MSG_RESERVE_SEATS_REQUEST (5)
格式：
  - 消息头（12字节）
  - 航班号（4字节）
  - 座位数（4字节）
```

#### 5.3.4 注册监控请求
```
消息类型：MSG_REGISTER_MONITOR_REQUEST (7)
格式：
  - 消息头（12字节）
  - 航班号（4字节）
  - 监控时间间隔（秒）（4字节）
```

#### 5.3.5 查询剩余座位数请求
```
消息类型：MSG_GET_SEAT_COUNT_REQUEST (10)
格式：
  - 消息头（12字节）
  - 航班号（4字节）
```

#### 5.3.6 取消预订请求
```
消息类型：MSG_CANCEL_RESERVATION_REQUEST (12)
格式：
  - 消息头（12字节）
  - 航班号（4字节）
  - 取消座位数（4字节）
```

### 5.4 响应消息格式

#### 5.4.1 通用响应格式
```
所有响应消息都包含：
  - 消息头（12字节）
  - 状态码（4字节）：0=成功，非0=错误
  - 错误信息长度（4字节）：仅当状态码非0时
  - 错误信息（变长）：仅当状态码非0时
  - 响应数据（变长）：仅当状态码为0时
```

#### 5.4.2 查询航班响应
```
消息类型：MSG_QUERY_FLIGHT_RESPONSE (2)
格式（成功时）：
  - 消息头（12字节）
  - 状态码（4字节）= 0
  - 航班数量（4字节）
  - 航班号列表（每个4字节）
```

#### 5.4.3 查询航班详情响应
```
消息类型：MSG_GET_FLIGHT_INFO_RESPONSE (4)
格式（成功时）：
  - 消息头（12字节）
  - 状态码（4字节）= 0
  - 出发地长度（4字节）
  - 出发地（变长）
  - 目的地长度（4字节）
  - 目的地（变长）
  - 出发时间（20字节：5个整数）
  - 票价（4字节，float）
  - 可用座位数（4字节）
```

#### 5.4.4 预订座位响应
```
消息类型：MSG_RESERVE_SEATS_RESPONSE (6)
格式（成功时）：
  - 消息头（12字节）
  - 状态码（4字节）= 0
  - 剩余座位数（4字节）
```

#### 5.4.5 注册监控响应
```
消息类型：MSG_REGISTER_MONITOR_RESPONSE (8)
格式（成功时）：
  - 消息头（12字节）
  - 状态码（4字节）= 0
```

#### 5.4.6 回调更新消息
```
消息类型：MSG_CALLBACK_UPDATE (9)
格式：
  - 消息头（12字节）
  - 航班号（4字节）
  - 新的座位数（4字节）
  - 剩余监控时间（秒）（4字节）
```

#### 5.4.7 查询剩余座位数响应
```
消息类型：MSG_GET_SEAT_COUNT_RESPONSE (11)
格式（成功时）：
  - 消息头（12字节）
  - 状态码（4字节）= 0
  - 当前座位数（4字节）
```

#### 5.4.8 取消预订响应
```
消息类型：MSG_CANCEL_RESERVATION_RESPONSE (13)
格式（成功时）：
  - 消息头（12字节）
  - 状态码（4字节）= 0
  - 当前座位数（4字节）
```

### 5.5 错误码定义

```cpp
enum ErrorCode {
    ERR_SUCCESS = 0,              // 成功
    ERR_INVALID_INPUT = 1,        // 无效输入
    ERR_FLIGHT_NOT_FOUND = 2,     // 航班不存在
    ERR_NO_FLIGHTS_FOUND = 3,     // 无匹配航班
    ERR_INSUFFICIENT_SEATS = 4,   // 座位不足
    ERR_INVALID_SEAT_COUNT = 5,   // 无效座位数
    ERR_INVALID_MONITOR_INTERVAL = 6,  // 无效监控时间
    ERR_SERVER_ERROR = 99,        // 服务器内部错误
};
```

### 5.6 字节序规范

**所有整数和浮点数在网络传输时都使用大端序（网络字节序）。**

使用以下函数进行转换：
```cpp
#include <arpa/inet.h>

// 主机字节序 → 网络字节序
uint32_t htonl(uint32_t hostlong);  // 32位整数
uint16_t htons(uint16_t hostshort); // 16位整数

// 网络字节序 → 主机字节序
uint32_t ntohl(uint32_t netlong);
uint16_t ntohs(uint16_t netshort);
```

**浮点数转换：**
```cpp
// float 转网络字节序
uint32_t float_to_network(float f) {
    uint32_t i;
    memcpy(&i, &f, sizeof(float));
    return htonl(i);
}

// 网络字节序转 float
float network_to_float(uint32_t i) {
    i = ntohl(i);
    float f;
    memcpy(&f, &i, sizeof(float));
    return f;
}
```

---

## 6. API 接口设计

### 6.1 客户端 API

```cpp
class FlightClient {
public:
    // 初始化客户端
    FlightClient(const std::string& server_ip, int server_port);
    ~FlightClient();
    
    // 服务 1：查询航班
    int queryFlight(const std::string& source, 
                    const std::string& destination,
                    std::vector<int>& flight_ids);
    
    // 服务 2：查询航班详情
    int getFlightInfo(int flight_id, Flight& flight_info);
    
    // 服务 3：预订座位
    int reserveSeats(int flight_id, int seat_count, int& remaining_seats);
    
    // 服务 4：注册监控（阻塞直到监控到期）
    int registerMonitor(int flight_id, int interval_seconds,
                        std::function<void(int, int, int)> callback);
    
    // 服务 5：查询剩余座位数
    int getSeatCount(int flight_id, int& seat_count);
    
    // 服务 6：取消预订
    int cancelReservation(int flight_id, int seat_count, int& current_seats);
    
    // 设置调用语义
    void setInvocationSemantic(bool at_least_once);
    
    // 设置消息丢失概率（用于测试）
    void setLossRate(double rate);
    
private:
    // 发送请求并接收响应
    int sendRequest(const std::vector<uint8_t>& request,
                    std::vector<uint8_t>& response);
    
    // 重试逻辑（At-Least-Once）
    int sendRequestWithRetry(const std::vector<uint8_t>& request,
                             std::vector<uint8_t>& response);
};
```

### 6.2 服务器 API

```cpp
class FlightServer {
public:
    // 初始化服务器
    FlightServer(int port);
    ~FlightServer();
    
    // 启动服务器（阻塞）
    void start();
    
    // 停止服务器
    void stop();
    
    // 添加测试航班
    void addFlight(const Flight& flight);
    
    // 设置调用语义
    void setInvocationSemantic(bool at_least_once);
    
    // 设置消息丢失概率（用于测试）
    void setLossRate(double rate);
    
private:
    // 处理请求
    void handleRequest(const std::vector<uint8_t>& request,
                       std::vector<uint8_t>& response,
                       const struct sockaddr_in& client_addr);
    
    // 各服务实现
    void handleQueryFlight(const std::vector<uint8_t>& request,
                           std::vector<uint8_t>& response);
    
    void handleGetFlightInfo(const std::vector<uint8_t>& request,
                             std::vector<uint8_t>& response);
    
    void handleReserveSeats(const std::vector<uint8_t>& request,
                            std::vector<uint8_t>& response);
    
    void handleRegisterMonitor(const std::vector<uint8_t>& request,
                               std::vector<uint8_t>& response,
                               const struct sockaddr_in& client_addr);
    
    void handleGetSeatCount(const std::vector<uint8_t>& request,
                            std::vector<uint8_t>& response);
    
    void handleCancelReservation(const std::vector<uint8_t>& request,
                                 std::vector<uint8_t>& response);
    
    // 通知监控客户端
    void notifyMonitors(int flight_id, int new_seat_count);
    
    // 清理过期监控
    void cleanupExpiredMonitors();
};
```

---

## 7. 测试数据

### 7.1 初始航班数据

```cpp
// 初始化 10 个测试航班
Flight flights[] = {
    {1001, "Beijing", "Shanghai", {2026, 10, 20, 8, 30}, 800.50, 120},
    {1002, "Beijing", "Shanghai", {2026, 10, 20, 10, 0}, 850.00, 100},
    {1003, "Beijing", "Shanghai", {2026, 10, 20, 14, 30}, 900.00, 80},
    {2001, "Shanghai", "Beijing", {2026, 10, 21, 9, 0}, 820.00, 110},
    {2002, "Shanghai", "Beijing", {2026, 10, 21, 15, 0}, 870.50, 90},
    {3001, "Guangzhou", "Chengdu", {2026, 10, 22, 7, 30}, 1200.00, 150},
    {3002, "Guangzhou", "Chengdu", {2026, 10, 22, 13, 0}, 1250.00, 130},
    {4001, "Shenzhen", "Xi'an", {2026, 10, 23, 11, 0}, 1100.00, 100},
    {5001, "Hangzhou", "Wuhan", {2026, 10, 24, 16, 0}, 700.00, 140},
    {6001, "Nanjing", "Kunming", {2026, 10, 25, 12, 30}, 1500.00, 120},
};
```

---

## 8. 验收标准

### 8.1 功能验收
- [ ] 服务 1-6 所有功能正确实现
- [ ] 错误处理正确
- [ ] 回调机制工作正常
- [ ] 多客户端并发监控正常

### 8.2 调用语义验收
- [ ] At-Most-Once 语义实现正确
- [ ] At-Least-Once 语义实现正确
- [ ] 去重机制工作正常
- [ ] 能够演示消息丢失场景

### 8.3 性能验收
- [ ] 响应时间 < 1 秒
- [ ] 支持 10 个并发客户端
- [ ] 支持 5 个并发监控

### 8.4 文档验收
- [ ] 设计文档完整
- [ ] 测试报告完整
- [ ] 实验报告完整
- [ ] 代码注释清晰

---

## 9. 附录

### 9.1 术语表
- **Marshalling（编组）**：将数据结构转换为字节流的过程
- **Unmarshalling（解组）**：将字节流转换回数据结构的过程
- **Idempotent（幂等）**：操作执行一次和执行多次效果相同
- **Callback（回调）**：服务器主动向客户端发送消息
- **Invocation Semantic（调用语义）**：定义请求执行的保证

### 9.2 参考资料
- 课程讲义：第 2-3 章（进程间通信、远程调用）
- UDP Socket 编程指南
- C++ 网络编程

---

**文档版本历史：**
- v1.0 (2026-09-28): 初始版本
