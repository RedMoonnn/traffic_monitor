# 我的自定义函数说明

## 1. 基础配置与全局变量

### 关键配置
```c
#define MAX_SECONDS 40   // 统计窗口大小（秒）
#define MAX_IP 100       // 支持最多统计的IP数量
```

### 主要结构体
```c
// 单秒流量统计结构体
typedef struct {
    unsigned long bytes;    // 本秒字节数
    unsigned long pkts;     // 本秒数据包数
} Stat;

// 分IP分方向统计结构体
typedef struct {
    unsigned long bytes;    // 本秒字节数
    unsigned long pkts;     // 本秒包数
    unsigned long peak;     // 40秒窗口内最大每秒bytes
    unsigned long total;    // 累计总流量（字节）
    float avg2;             // 2秒平均速率
    float avg10;            // 10秒平均速率
    float avg40;            // 40秒平均速率
    unsigned long bytes_history[MAX_SECONDS]; // 40秒历史环形缓冲区
    int history_idx;        // 当前环形索引
} PerIPStat;
```

### 全局变量
```c
Stat stats[MAX_SECONDS] = {0};  // 全局每秒统计环形缓冲区
unsigned long peak = 0;         // 全局峰值速率
unsigned long total = 0;        // 全局累计流量
int cur_sec = 0;                // 当前秒的索引
char *api_url = NULL;           // 后端API地址
```

---

## 2. 工具函数

### discard_response
```c
size_t discard_response(void *ptr, size_t size, size_t nmemb, void *userdata)
```
**功能**：丢弃 HTTP 响应内容的回调函数  
**参数**：
- `ptr`: 响应数据指针
- `size`: 单个数据块大小
- `nmemb`: 数据块数量
- `userdata`: 用户自定义参数（未使用）  
**返回值**：实际处理的数据字节数  
**使用场景**：用于 libcurl 的响应处理，防止响应内容输出到终端

---

## 3. 信号处理

### handle_sigint
```c
void handle_sigint(int sig)
```
**功能**：处理 Ctrl+C 信号，实现优雅退出  
**参数**：
- `sig`: 信号编号  
**工作流程**：
1. 第一次收到 SIGINT：设置 running=0，允许主循环退出
2. 再次收到 SIGINT：强制退出程序  
**使用示例**：
```c
signal(SIGINT, handle_sigint);
```

---

## 4. HTTP 上报相关

### send_stats_to_api
```c
void send_stats_to_api(unsigned long total, unsigned long peak, float avg2, float avg10, float avg40)
```
**功能**：发送全局统计数据到后端 API  
**参数**：
- `total`: 累计总流量
- `peak`: 峰值速率
- `avg2/avg10/avg40`: 不同窗口平均速率  
**使用示例**：
```c
send_stats_to_api(1000, 100, 50.5, 45.2, 40.1);
```

### send_packet_to_api
```c
void send_packet_to_api(const char *type, const char *src, int sport, const char *dst, int dport, int size)
```
**功能**：发送单个数据包信息到后端 API  
**参数**：
- `type`: 协议类型（TCP/UDP/ICMP）
- `src`: 源IP
- `sport`: 源端口
- `dst`: 目标IP
- `dport`: 目标端口
- `size`: 数据包大小  
**使用示例**：
```c
send_packet_to_api("TCP", "192.168.1.1", 80, "192.168.1.2", 443, 1500);
```

### get_ip_index
```c
int get_ip_index(const char *ip)
```
**功能**：获取或创建 IP 索引  
**参数**：
- `ip`: IP 地址字符串  
**返回值**：
- 成功：返回 IP 的索引
- 失败：返回 -1  
**工作流程**：
1. 查找 IP 是否已存在
2. 若存在，返回其索引
3. 若不存在且未超过最大 IP 数，创建新索引
4. 初始化新 IP 的统计数据结构  
**使用示例**：
```c
int idx = get_ip_index("192.168.1.1");
if (idx >= 0) {
    // 使用索引进行统计
}
```

### send_per_ip_stats_to_api
```c
void send_per_ip_stats_to_api()
```
**功能**：发送所有 IP 分方向统计数据到后端 API  
**工作流程**：
1. 遍历所有已记录的 IP
2. 分别统计发送和接收方向的数据
3. 组装为 JSON 数组
4. 通过 HTTP POST 发送到后端  
**数据格式**：
```json
[
    {
        "ip": "192.168.1.1",
        "direction": "send",
        "total": 1000,
        "peak": 100,
        "avg2": 50.5,
        "avg10": 45.2,
        "avg40": 40.1
    },
    {
        "ip": "192.168.1.1",
        "direction": "recv",
        "total": 2000,
        "peak": 200,
        "avg2": 100.5,
        "avg10": 95.2,
        "avg40": 90.1
    }
]
```

---

## 5. 流量统计相关

### update_per_ip_peak
```c
void update_per_ip_peak()
```
**功能**：统计每个 IP 分方向 40 秒窗口内的峰值速率  
**工作流程**：
1. 遍历所有已记录的 IP
2. 推进环形缓冲区
3. 更新历史数据
4. 计算 40 秒窗口内的最大值  
**使用场景**：每秒调用一次，用于更新峰值统计

### print_stats
```c
void print_stats()
```
**功能**：每秒打印并上报统计信息  
**工作流程**：
1. 计算不同窗口（2秒/10秒/40秒）的平均速率
2. 更新分 IP 分方向统计
3. 发送全局统计数据
4. 发送分 IP 统计数据
5. 清零本秒统计，准备下一个统计周期  
**调用时机**：每秒调用一次

---

## 6. 数据包处理

### packet_handler
```c
void packet_handler(u_char *user_data, const struct pcap_pkthdr *pkthdr, const u_char *packet)
```
**功能**：数据包处理主回调函数  
**参数**：
- `user_data`: 用户数据（未使用）
- `pkthdr`: 数据包头信息
- `packet`: 数据包内容  
**工作流程**：
1. 解析 IP 头，获取源 IP 和目标 IP
2. 根据协议类型（TCP/UDP/ICMP）解析相应的协议头
3. 打印数据包信息
4. 上报数据包信息到后端
5. 更新统计信息（全局和分 IP）
6. 检查是否需要进入新的统计周期  
**使用场景**：由 libpcap 库在捕获到数据包时自动调用 