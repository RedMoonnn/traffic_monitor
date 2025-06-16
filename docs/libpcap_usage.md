# libpcap 库函数使用说明

### 1. 基础函数

#### pcap_open_live
```c
pcap_t *pcap_open_live(const char *device, int snaplen, int promisc, int to_ms, char *errbuf)
```
**功能**：打开网络接口进行数据包捕获  
**参数**：
- `device`: 网络接口名称（如 "eth0"）
- `snaplen`: 捕获的最大字节数
- `promisc`: 是否启用混杂模式（1:启用，0:禁用）
- `to_ms`: 超时时间（毫秒）
- `errbuf`: 错误信息缓冲区  
**返回值**：
- 成功：返回 pcap 会话句柄
- 失败：返回 NULL，错误信息存储在 errbuf 中  
**使用示例**：
```c
char errbuf[PCAP_ERRBUF_SIZE];
pcap_t *handle = pcap_open_live("eth0", BUFSIZ, 1, 1000, errbuf);
if (handle == NULL) {
    fprintf(stderr, "无法打开设备: %s\n", errbuf);
    return;
}
```

#### pcap_compile
```c
int pcap_compile(pcap_t *p, struct bpf_program *fp, const char *str, int optimize, bpf_u_int32 netmask)
```
**功能**：编译 BPF 过滤表达式  
**参数**：
- `p`: pcap 会话句柄
- `fp`: 编译后的 BPF 程序
- `str`: 过滤表达式（如 "tcp port 80"）
- `optimize`: 是否优化（1:优化，0:不优化）
- `netmask`: 网络掩码  
**返回值**：
- 0: 成功
- -1: 失败  
**使用示例**：
```c
struct bpf_program fp;
if (pcap_compile(handle, &fp, "tcp port 80", 0, net) == -1) {
    fprintf(stderr, "无法解析过滤表达式: %s\n", pcap_geterr(handle));
    return;
}
```

#### pcap_setfilter
```c
int pcap_setfilter(pcap_t *p, struct bpf_program *fp)
```
**功能**：设置过滤器  
**参数**：
- `p`: pcap 会话句柄
- `fp`: 编译后的 BPF 程序  
**返回值**：
- 0: 成功
- -1: 失败  
**使用示例**：
```c
if (pcap_setfilter(handle, &fp) == -1) {
    fprintf(stderr, "无法设置过滤器: %s\n", pcap_geterr(handle));
    return;
}
```

#### pcap_dispatch
```c
int pcap_dispatch(pcap_t *p, int cnt, pcap_handler callback, u_char *user)
```
**功能**：捕获并处理数据包  
**参数**：
- `p`: pcap 会话句柄
- `cnt`: 处理的数据包数量（-1 表示无限）
- `callback`: 回调函数
- `user`: 用户数据  
**返回值**：
- >0: 处理的数据包数量
- 0: 超时
- -1: 错误
- -2: 读取结束  
**使用示例**：
```c
pcap_dispatch(handle, -1, packet_handler, NULL);
```

### 2. 数据包处理回调函数

#### packet_handler
```c
void packet_handler(u_char *user_data, const struct pcap_pkthdr *pkthdr, const u_char *packet)
```
**功能**：处理捕获的数据包  
**参数**：
- `user_data`: 用户数据
- `pkthdr`: 数据包头信息
  - `ts`: 时间戳
  - `caplen`: 实际捕获的数据长度
  - `len`: 数据包实际长度
- `packet`: 数据包内容  
**使用示例**：
```c
void packet_handler(u_char *user_data, const struct pcap_pkthdr *pkthdr, const u_char *packet) {
    // 解析以太网头
    struct ether_header *eth_header = (struct ether_header *)packet;
    
    // 解析 IP 头
    struct ip *ip_header = (struct ip *)(packet + sizeof(struct ether_header));
    
    // 获取源 IP 和目标 IP
    char src_ip[INET_ADDRSTRLEN], dst_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(ip_header->ip_src), src_ip, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &(ip_header->ip_dst), dst_ip, INET_ADDRSTRLEN);
    
    // 根据协议类型处理
    switch (ip_header->ip_p) {
        case IPPROTO_TCP:
            // 处理 TCP 包
            break;
        case IPPROTO_UDP:
            // 处理 UDP 包
            break;
        case IPPROTO_ICMP:
            // 处理 ICMP 包
            break;
    }
}
```

### 3. 资源清理函数

#### pcap_freecode
```c
void pcap_freecode(struct bpf_program *fp)
```
**功能**：释放 BPF 程序  
**参数**：
- `fp`: 要释放的 BPF 程序  
**使用示例**：
```c
pcap_freecode(&fp);
```

#### pcap_close
```c
void pcap_close(pcap_t *p)
```
**功能**：关闭 pcap 会话  
**参数**：
- `p`: 要关闭的 pcap 会话句柄  
**使用示例**：
```c
pcap_close(handle);
```

### 4. 完整使用示例

```c
int main(int argc, char *argv[]) {
    char *dev;  // 网络接口名称
    char errbuf[PCAP_ERRBUF_SIZE];  // 错误信息缓冲区
    struct bpf_program fp;  // BPF 过滤器程序
    bpf_u_int32 net = 0;    // 网络地址
    pcap_t *handle;         // 数据包捕获句柄

    // 获取网络接口
    dev = pcap_lookupdev(errbuf);
    if (dev == NULL) {
        fprintf(stderr, "无法找到默认设备: %s\n", errbuf);
        return 1;
    }

    // 打开网络接口
    handle = pcap_open_live(dev, BUFSIZ, 1, 1000, errbuf);
    if (handle == NULL) {
        fprintf(stderr, "无法打开设备 %s: %s\n", dev, errbuf);
        return 2;
    }

    // 编译过滤表达式
    if (pcap_compile(handle, &fp, "ip", 0, net) == -1) {
        fprintf(stderr, "无法解析过滤表达式: %s\n", pcap_geterr(handle));
        return 2;
    }

    // 设置过滤器
    if (pcap_setfilter(handle, &fp) == -1) {
        fprintf(stderr, "无法设置过滤器: %s\n", pcap_geterr(handle));
        return 2;
    }

    // 开始捕获数据包
    pcap_loop(handle, -1, packet_handler, NULL);

    // 清理资源
    pcap_freecode(&fp);
    pcap_close(handle);
    return 0;
}
```

### 5. 在我们的代码中的应用

在我们的流量监控程序中，主要使用 libpcap 进行以下操作：

1. 初始化捕获：
```c
// 打开网络接口
handle = pcap_open_live(dev, BUFSIZ, 1, 1000, errbuf);
if (handle == NULL) {
    fprintf(stderr, "无法打开设备 %s: %s\n", dev, errbuf);
    return 2;
}

// 编译过滤表达式
if (pcap_compile(handle, &fp, filter_exp, 0, net) == -1) {
    fprintf(stderr, "无法解析过滤表达式 %s: %s\n", filter_exp, pcap_geterr(handle));
    return 2;
}

// 设置过滤器
if (pcap_setfilter(handle, &fp) == -1) {
    fprintf(stderr, "无法设置过滤器: %s\n", pcap_geterr(handle));
    return 2;
}
```

2. 数据包处理：
```c
void packet_handler(u_char *user_data, const struct pcap_pkthdr *pkthdr, const u_char *packet) {
    // 解析 IP 头
    struct ip *ip_hdr = (struct ip *)(packet + 14);  // 跳过以太网头
    char src_ip[INET_ADDRSTRLEN], dst_ip[INET_ADDRSTRLEN];
    
    // 获取源 IP 和目标 IP
    inet_ntop(AF_INET, &(ip_hdr->ip_src), src_ip, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &(ip_hdr->ip_dst), dst_ip, INET_ADDRSTRLEN);
    
    // 根据协议类型处理
    switch (ip_hdr->ip_p) {
        case IPPROTO_TCP: {
            struct tcphdr *tcp_hdr = (struct tcphdr *)((u_char *)ip_hdr + (ip_hdr->ip_hl << 2));
            printf("[TCP] %s:%d -> %s:%d (%d B)\n", 
                   src_ip, ntohs(tcp_hdr->th_sport), 
                   dst_ip, ntohs(tcp_hdr->th_dport), 
                   pkthdr->len);
            break;
        }
        // ... 其他协议处理 ...
    }
    
    // 更新统计信息
    update_stats(src_ip, dst_ip, pkthdr->len);
}
```

这些函数使得我们的程序能够实时捕获和分析网络数据包，实现流量监控功能。 