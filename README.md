# 流量监控系统与使用指南

一个基于OpenWrt的网络流量监控系统，包含实时数据采集、后端服务和Web前端界面。本文档涵盖环境准备、依赖安装、交叉编译、部署、运行、调试、功能说明和常见问题。

## 目录结构

```
traffic_monitor/
├── src/                    # 源代码目录
│   ├── traffic_monitor.c   # 流量监控程序源码
│   ├── traffic_monitor     # 编译后的可执行文件
│   └── Makefile           # 编译配置文件
├── web/                    # Web应用目录
│   ├── frontend/          # 前端代码
│   │   ├── index.html     # 主页面
│   │   └── app.js         # 前端逻辑
│   └── backend/           # 后端代码
│       ├── app.py         # Flask后端服务
│       └── requirements.txt # Python依赖
├── docs/                  # 文档目录
│   └── traffic_monitor_guide.md # 详细使用说明（已合并）
└── README.md              # 项目说明文档
```

## 一、环境准备

### 开发环境
- Ubuntu 20.04 或更高版本
- OpenWrt SDK 24.10.0
- Python 3.7+
- Node.js 12+
- libpcap 开发库
- libcurl 开发库（由 curl 包提供）
- 交叉编译工具链

### OpenWrt设备
- OpenWrt 21.02或更高版本
- libpcap
- libcurl

## 二、依赖安装与交叉编译

### 1. 安装 OpenWrt SDK
```bash
wget https://downloads.openwrt.org/releases/24.10.0/targets/x86/64/openwrt-sdk-24.10.0-x86-64_gcc-13.3.0_musl.Linux-x86_64.tar.zst
tar xf openwrt-sdk-24.10.0-x86-64_gcc-13.3.0_musl.Linux-x86_64.tar.zst
```

### 2. 设置交叉编译环境
```bash
export STAGING_DIR=/home/chy_wsl/openwrt/openwrt-sdk-24.10.0-x86-64_gcc-13.3.0_musl.Linux-x86_64/staging_dir
export PATH=$STAGING_DIR/toolchain-x86_64_gcc-13.3.0_musl/bin:$PATH
x86_64-openwrt-linux-musl-gcc --version
```

### 3. 编译 libpcap 和 curl（libcurl）
```bash
cd openwrt-sdk-24.10.0-x86-64_gcc-13.3.0_musl.Linux-x86_64
./scripts/feeds update -a
./scripts/feeds install libpcap curl
make package/libpcap/compile V=s
make package/curl/compile V=s
```

### 4. 查找头文件和库文件路径
```bash
find $STAGING_DIR -name pcap.h
find $STAGING_DIR -name curl.h
find $STAGING_DIR -name libpcap.so
find $STAGING_DIR -name libcurl.so
```

### 5. 编译流量监控程序
```bash
cd traffic_monitor/src
make clean
make
```

## 三、部署与运行

### 1. 安装OpenWrt依赖
```bash
opkg update
opkg install libpcap libcurl
```

### 2. 传输可执行文件到 OpenWrt
```bash
scp traffic_monitor root@192.168.211.90:/root/
```

### 3. 在 OpenWrt 上运行程序
```bash
chmod +x /root/traffic_monitor
./traffic_monitor eth0 "ip" "http://172.30.127.8:5000/api"
```

### 4. 启动后端服务
```bash
cd web/backend
python app.py
```

### 5. 启动前端服务
```bash
cd web/frontend
python -m http.server 8080
```

### 6. 在浏览器中访问
```
http://localhost:8080
```

## 四、参数说明
- `<网络接口>`：要监控的网络接口名称（如 eth0）。
- `[过滤表达式]`：可选，BPF 过滤表达式，默认为 "ip"。
- `[API服务器地址]`：可选，前端 API 服务器地址，用于推送统计数据。

## 五、功能特性
- 实时捕获网络数据包
- 解析 IP、TCP、UDP、ICMP 协议头
- 统计总流量、峰值流量、平均流量（2秒、10秒、40秒窗口）
- 支持自定义过滤规则
- 将统计数据通过 HTTP POST 推送到前端界面
- WebSocket 实时更新
- 历史数据记录

## 六、测试样例

### 本地测试
```bash
sudo ./traffic_monitor eth0 "ip"
sudo ./traffic_monitor eth0 "port 80"
sudo ./traffic_monitor eth0 "host 192.168.1.1"
```

### OpenWrt 测试
```bash
./traffic_monitor eth0 "ip"
./traffic_monitor eth0 "port 80"
./traffic_monitor eth0 "host 192.168.1.1"
```

## 七、调试与常见问题

- 程序需要 root 权限才能捕获数据包。
- 确保 OpenWrt 设备已安装 libpcap 和 libcurl 动态库。
- 如果前端界面未更新，检查 API 服务器地址和网络连接。
- 程序会输出每个数据包的详细信息，以及每秒的流量统计。
- 如果 HTTP 请求失败，会输出 libcurl 的错误信息。
- 检查文件权限、网络接口名称、依赖库是否安装。
- 确保 OpenWrt 设备能够访问 WSL 的 IP 地址，防火墙端口已放行。

## 八、开发说明

### 后端API
- GET /api/stats - 获取最新统计数据
- GET /api/history - 获取历史数据
- POST /api/update - 更新统计数据
- POST /api/packets - 更新数据包信息

### WebSocket事件
- stats_update - 统计数据更新
- packet_update - 数据包信息更新

## 九、原理简介

- 使用 libpcap 库打开网络接口，设置过滤规则，捕获数据包。
- 解析 IP 头，获取源 IP 和目标 IP，根据协议类型解析 TCP/UDP/ICMP 协议头。
- 使用环形缓冲区存储每秒流量数据，计算总流量、峰值、平均流量。
- 使用 libcurl 将统计数据通过 HTTP POST 推送到前端接口。
- 支持自定义 API 服务器地址。

## 十、总结

Traffic Monitor 是一个功能强大的流量监控工具，适合网络流量分析和调试。通过交叉编译，可以在 OpenWrt 设备上运行，实现实时流量监控和数据可视化。

## 前端使用说明

1. 进入 `web/frontend` 目录，启动本地静态服务器（如 Python http.server）：
   ```bash
   python -m http.server 8080
   ```
2. 浏览器访问 `http://localhost:8080`，即可看到流量监控可视化界面。
3. 前端会每5秒自动从后端接口 `/api/update` 拉取最新流量数据，动态刷新表格和图表。
4. 如需修改后端接口地址，请编辑 `index.html` 中的 `fetch` URL。
5. 支持多IP多方向的流量统计，图表和表格均自动适配。 