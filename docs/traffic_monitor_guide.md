# Traffic Monitor 使用指南

## 简介

Traffic Monitor 是一个基于 libpcap 的流量监控程序，能够实时捕获和分析网络数据包，统计流量信息，并将数据推送到前端界面进行可视化展示。

## 功能特点

- 实时捕获网络数据包
- 解析 IP、TCP、UDP、ICMP 协议头
- 统计总流量、峰值流量、平均流量（2秒、10秒、40秒窗口）
- 支持自定义过滤规则
- 将统计数据通过 HTTP POST 推送到前端界面

## 实现原理

### 1. 数据包捕获

- 使用 libpcap 库打开网络接口，设置过滤规则，捕获数据包。
- 通过 `pcap_dispatch` 回调函数 `packet_handler` 处理每个数据包。

### 2. 数据包解析

- 解析 IP 头，获取源 IP 和目标 IP。
- 根据协议类型（TCP、UDP、ICMP）解析相应的协议头，提取端口号等信息。

### 3. 流量统计

- 使用环形缓冲区存储每秒的流量数据。
- 计算总流量、峰值流量、2秒、10秒、40秒的平均流量。

### 4. 数据推送

- 使用 libcurl 将统计数据通过 HTTP POST 推送到前端接口。
- 支持自定义 API 服务器地址。

## 使用方法

### 编译

#### 本地编译

1. 安装依赖：
   ```bash
   sudo apt-get install libpcap-dev libcurl4-openssl-dev
   ```

2. 编译程序：
   ```bash
   cd traffic_monitor/src
   make
   ```

#### 交叉编译（OpenWrt）

1. 安装 OpenWrt SDK：
   ```bash
   # 下载并解压 OpenWrt SDK
   tar xf openwrt-sdk-xxx.tar.gz
   cd openwrt-sdk-xxx
   ```

2. 安装依赖：
   ```bash
   ./scripts/feeds update
   ./scripts/feeds install libpcap libcurl
   ```

3. 编译程序：
   ```bash
   cd traffic_monitor/src
   make
   ```

### 在 WSL 上通过 SCP 传输代码到 OpenWrt

1. **确保 WSL 和 OpenWrt 网络互通**：
   - 确保 WSL 和 OpenWrt 设备在同一网络下，能够互相访问。

2. **使用 SCP 传输文件**：
   ```bash
   scp src/traffic_monitor root@192.168.211.90:/root/
   ```

3. **在 OpenWrt 上运行程序**：
   ```bash
   chmod +x /root/traffic_monitor
   ./traffic_monitor eth0 "ip" "http://172.30.127.8:5000/api/update"
   ```

### 运行

1. 本地运行：
   ```bash
   sudo ./traffic_monitor <网络接口> [过滤表达式] [API服务器地址]
   ```

2. OpenWrt 运行：
   ```bash
   ./traffic_monitor eth0 "ip" "http://172.30.127.8:5000/api/update"
   ```

### 参数说明

- `<网络接口>`：要监控的网络接口名称（如 eth0）。
- `[过滤表达式]`：可选，BPF 过滤表达式，默认为 "ip"。
- `[API服务器地址]`：可选，前端 API 服务器地址，用于推送统计数据。

## 测试样例

### 本地测试

1. **捕获所有 IP 流量**：
   ```bash
   sudo ./traffic_monitor eth0 "ip"
   ```

2. **捕获特定端口的流量**：
   ```bash
   sudo ./traffic_monitor eth0 "port 80"
   ```

3. **捕获特定 IP 的流量**：
   ```bash
   sudo ./traffic_monitor eth0 "host 192.168.1.1"
   ```

### OpenWrt 测试

1. **捕获所有 IP 流量**：
   ```bash
   ./traffic_monitor eth0 "ip"
   ```

2. **捕获特定端口的流量**：
   ```bash
   ./traffic_monitor eth0 "port 80"
   ```

3. **捕获特定 IP 的流量**：
   ```bash
   ./traffic_monitor eth0 "host 192.168.1.1"
   ```

## 注意事项

- 程序需要 root 权限才能捕获数据包。
- 确保 OpenWrt 设备已安装 libpcap 和 libcurl 动态库。
- 如果前端界面未更新，检查 API 服务器地址和网络连接。

## 调试

- 程序会输出每个数据包的详细信息，以及每秒的流量统计。
- 如果 HTTP 请求失败，会输出 libcurl 的错误信息。

## 总结

Traffic Monitor 是一个功能强大的流量监控工具，适合网络流量分析和调试。通过交叉编译，可以在 OpenWrt 设备上运行，实现实时流量监控和数据可视化。

