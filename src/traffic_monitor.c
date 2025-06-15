/**
 * 流量监控程序
 * 功能：实时捕获和分析网络数据包，统计流量信息
 * 实现原理：
 * 1. 使用libpcap库捕获网络数据包
 * 2. 解析数据包头部信息（IP、TCP、UDP、ICMP）
 * 3. 统计流量数据（总流量、峰值、平均速率）
 * 4. 支持自定义过滤规则
 * 
 * libpcap主要函数说明：
 * 1. pcap_open_live: 打开网络接口进行实时捕获
 * 2. pcap_compile: 编译BPF过滤表达式
 * 3. pcap_setfilter: 设置过滤器
 * 4. pcap_dispatch: 捕获并处理数据包
 * 5. pcap_freecode: 释放BPF程序
 * 6. pcap_close: 关闭捕获会话
 * 
 * libpcap主要结构体说明：
 * 1. pcap_t: 捕获会话句柄
 * 2. pcap_pkthdr: 数据包头信息（时间戳、长度等）
 * 3. bpf_program: 编译后的BPF过滤程序
 */

#include <pcap.h>           // libpcap库，用于网络数据包捕获
#include <stdio.h>          // 标准输入输出
#include <stdlib.h>         // 标准库函数
#include <string.h>         // 字符串处理函数
#include <signal.h>         // 信号处理
#include <netinet/ip.h>     // IP协议头
#include <netinet/tcp.h>    // TCP协议头
#include <netinet/udp.h>    // UDP协议头
#include <netinet/icmp6.h>  // ICMP协议头
#include <arpa/inet.h>      // IP地址转换函数
#include <time.h>           // 时间相关函数
#include <curl/curl.h>      // 添加libcurl支持

// 定义统计时间窗口大小（40秒）
#define MAX_SECONDS 40

// 流量统计结构体
typedef struct {
    unsigned long bytes;    // 字节数
    unsigned long pkts;     // 数据包数
} Stat;

// 新增分IP分方向统计结构体，增加bytes_history和history_idx
typedef struct {
    unsigned long bytes;    // 本秒字节数
    unsigned long pkts;     // 数据包数
    unsigned long peak;     // 峰值流量（40秒窗口最大每秒bytes）
    unsigned long total;    // 总流量（字节）
    float avg2;             // 2秒平均
    float avg10;            // 10秒平均
    float avg40;            // 40秒平均
    unsigned long bytes_history[MAX_SECONDS]; // 40秒历史
    int history_idx; // 当前环形索引
} PerIPStat;

// 全局变量
Stat stats[MAX_SECONDS] = {0};  // 环形缓冲区存储每秒的统计信息
unsigned long peak = 0;         // 峰值流量（字节/秒）
unsigned long total = 0;        // 总流量（字节）
int cur_sec = 0;               // 当前统计时间窗口索引
static volatile sig_atomic_t running = 1;  // 程序运行状态标志
time_t last_print_time = 0;    // 上次打印统计信息的时间
time_t start_time = 0;  // 程序启动时间
char *api_url = NULL;  // API服务器基础地址

// 新增全局变量
#define MAX_IP 100
PerIPStat send_stats[MAX_IP] = {0};  // 发送统计
PerIPStat recv_stats[MAX_IP] = {0};  // 接收统计
char ip_list[MAX_IP][INET_ADDRSTRLEN];  // IP列表
int ip_count = 0;  // IP数量

// 在文件顶部添加丢弃响应内容的回调
size_t discard_response(void *ptr, size_t size, size_t nmemb, void *userdata) {
    return size * nmemb;
}

/**
 * 信号处理函数
 * 功能：处理Ctrl+C信号，实现优雅退出
 * 实现原理：
 * 1. 第一次收到SIGINT信号时，设置running=0，允许程序完成当前操作
 * 2. 如果再次收到SIGINT信号，则强制退出程序
 * @param sig 信号编号
 */
void handle_sigint(int sig) {
    if (running) {
        printf("\n正在停止捕获...\n");
        running = 0;
    } else {
        printf("\n强制退出...\n");
        exit(1);
    }
}

/**
 * 发送统计数据到API服务器
 */
void send_stats_to_api(unsigned long total, unsigned long peak, float avg2, float avg10, float avg40) {
    if (!api_url) return;

    CURL *curl = curl_easy_init();
    if (!curl) return;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_response);

    char url[512];
    snprintf(url, sizeof(url), "%s/update", api_url);

    char json_data[256];
    snprintf(json_data, sizeof(json_data),
             "{\"total\":%lu,\"peak\":%lu,\"avg2\":%.1f,\"avg10\":%.1f,\"avg40\":%.1f}",
             total, peak, avg2, avg10, avg40);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
}

/**
 * 发送数据包信息到API服务器
 */
void send_packet_to_api(const char *type, const char *src, int sport, const char *dst, int dport, int size) {
    if (!api_url) return;

    CURL *curl = curl_easy_init();
    if (!curl) return;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_response);

    char url[512];
    snprintf(url, sizeof(url), "%s/packets", api_url);

    char json_data[256];
    snprintf(json_data, sizeof(json_data),
             "{\"type\":\"%s\",\"src\":\"%s\",\"sport\":%d,\"dst\":\"%s\",\"dport\":%d,\"size\":%d}",
             type, src, sport, dst, dport, size);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
}

/**
 * 新增函数：获取或创建IP索引
 */
int get_ip_index(const char *ip) {
    for (int i = 0; i < ip_count; i++) {
        if (strcmp(ip_list[i], ip) == 0) return i;
    }
    if (ip_count < MAX_IP) {
        strncpy(ip_list[ip_count], ip, INET_ADDRSTRLEN - 1);
        ip_list[ip_count][INET_ADDRSTRLEN - 1] = '\0';
        // 新增初始化
        memset(send_stats[ip_count].bytes_history, 0, sizeof(send_stats[ip_count].bytes_history));
        memset(recv_stats[ip_count].bytes_history, 0, sizeof(recv_stats[ip_count].bytes_history));
        send_stats[ip_count].history_idx = -1;
        recv_stats[ip_count].history_idx = -1;
        return ip_count++;
    }
    return -1;
}

/**
 * 新增函数：发送分IP分方向统计数据到API服务器
 */
void send_per_ip_stats_to_api() {
    if (!api_url) return;

    CURL *curl = curl_easy_init();
    if (!curl) return;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_response);

    char url[512];
    snprintf(url, sizeof(url), "%s/update", api_url);

    // 构建JSON数组
    char json_data[4096] = "[";
    int first = 1;
    for (int i = 0; i < ip_count; i++) {
        if (send_stats[i].total > 0) {
            if (!first) strcat(json_data, ",");
            char tmp[2048];
            snprintf(tmp, sizeof(tmp),
                     "{\"ip\":\"%s\",\"direction\":\"send\",\"total\":%lu,\"peak\":%lu,\"avg2\":%.1f,\"avg10\":%.1f,\"avg40\":%.1f}",
                     ip_list[i], send_stats[i].total, send_stats[i].peak, send_stats[i].avg2, send_stats[i].avg10, send_stats[i].avg40);
            strcat(json_data, tmp);
            first = 0;
        }
        if (recv_stats[i].total > 0) {
            if (!first) strcat(json_data, ",");
            char tmp[2048];
            snprintf(tmp, sizeof(tmp),
                     "{\"ip\":\"%s\",\"direction\":\"recv\",\"total\":%lu,\"peak\":%lu,\"avg2\":%.1f,\"avg10\":%.1f,\"avg40\":%.1f}",
                     ip_list[i], recv_stats[i].total, recv_stats[i].peak, recv_stats[i].avg2, recv_stats[i].avg10, recv_stats[i].avg40);
            strcat(json_data, tmp);
            first = 0;
        }
    }
    strcat(json_data, "]");

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
}

/**
 * 在print_stats前后，统计每个IP的40秒峰值
 */
void update_per_ip_peak() {
    for (int i = 0; i < ip_count; i++) {
        // 推进环形缓冲区
        send_stats[i].history_idx = (send_stats[i].history_idx + 1) % MAX_SECONDS;
        send_stats[i].bytes_history[send_stats[i].history_idx] = send_stats[i].bytes;
        recv_stats[i].history_idx = (recv_stats[i].history_idx + 1) % MAX_SECONDS;
        recv_stats[i].bytes_history[recv_stats[i].history_idx] = recv_stats[i].bytes;
        // 统计40秒窗口最大
        unsigned long max_send = 0, max_recv = 0;
        for (int j = 0; j < MAX_SECONDS; j++) {
            if (send_stats[i].bytes_history[j] > max_send) max_send = send_stats[i].bytes_history[j];
            if (recv_stats[i].bytes_history[j] > max_recv) max_recv = recv_stats[i].bytes_history[j];
        }
        send_stats[i].peak = max_send;
        recv_stats[i].peak = max_recv;
    }
}

/**
 * 打印流量统计信息
 * 功能：计算并显示不同时间窗口的平均流量
 * 实现原理：
 * 1. 使用环形缓冲区计算最近2秒、10秒和40秒的平均流量
 * 2. 根据实际运行时间计算平均值
 */
void print_stats() {
    unsigned long sum2 = 0, sum10 = 0, sum40 = 0;
    int count2 = 0, count10 = 0, count40 = 0;
    time_t current_time = time(NULL);
    time_t elapsed = current_time - start_time;

    // 统计前，先更新分IP分方向峰值
    update_per_ip_peak();

    // 计算全局2秒平均
    for (int i = 0; i < 2 && i < MAX_SECONDS; ++i) {
        sum2 += stats[(cur_sec - i + MAX_SECONDS) % MAX_SECONDS].bytes;
        count2++;
    }

    // 计算全局10秒平均
    for (int i = 0; i < 10 && i < MAX_SECONDS; ++i) {
        sum10 += stats[(cur_sec - i + MAX_SECONDS) % MAX_SECONDS].bytes;
        count10++;
    }

    // 计算全局40秒平均
    for (int i = 0; i < 40 && i < MAX_SECONDS; ++i) {
        sum40 += stats[(cur_sec - i + MAX_SECONDS) % MAX_SECONDS].bytes;
        count40++;
    }

    // 根据实际运行时间计算全局平均值
    float avg2 = 0, avg10 = 0, avg40 = 0;
    
    if (elapsed < 2) {
        avg2 = elapsed > 0 ? (float)sum2 / elapsed : 0;
    } else {
        avg2 = count2 > 0 ? (float)sum2 / count2 : 0;
    }

    if (elapsed < 10) {
        avg10 = elapsed > 0 ? (float)sum10 / elapsed : 0;
    } else {
        avg10 = count10 > 0 ? (float)sum10 / count10 : 0;
    }

    if (elapsed < 40) {
        avg40 = elapsed > 0 ? (float)sum40 / elapsed : 0;
    } else {
        avg40 = count40 > 0 ? (float)sum40 / count40 : 0;
    }

    // 更新分IP分方向统计
    for (int i = 0; i < ip_count; i++) {
        send_stats[i].avg2 = avg2;
        send_stats[i].avg10 = avg10;
        send_stats[i].avg40 = avg40;
        recv_stats[i].avg2 = avg2;
        recv_stats[i].avg10 = avg10;
        recv_stats[i].avg40 = avg40;
    }

    // 发送全局统计数据到API服务器
    send_stats_to_api(total, peak, avg2, avg10, avg40);

    // 发送分IP分方向统计数据到API服务器
    send_per_ip_stats_to_api();

    // 统计完后，清零本秒bytes，准备下一个秒窗口
    for (int i = 0; i < ip_count; i++) {
        send_stats[i].bytes = 0;
        recv_stats[i].bytes = 0;
    }
}

/**
 * 数据包处理函数
 * 功能：解析数据包并更新统计信息
 * 实现原理：
 * 1. 解析IP头，获取源IP和目标IP
 * 2. 根据协议类型（TCP/UDP/ICMP）解析相应的协议头
 * 3. 更新流量统计信息
 * 
 * 参数说明：
 * @param user_data 用户数据（未使用）
 * @param pkthdr pcap_pkthdr结构体，包含：
 *              - ts: 时间戳
 *              - caplen: 实际捕获的数据长度
 *              - len: 数据包实际长度
 * @param packet 数据包内容
 */
void packet_handler(u_char *user_data, const struct pcap_pkthdr *pkthdr, const u_char *packet) {
    // 解析IP头（跳过14字节的以太网头）
    struct ip *ip_hdr = (struct ip *)(packet + 14);
    char src_ip[INET_ADDRSTRLEN], dst_ip[INET_ADDRSTRLEN];
    // 检查数据包长度是否足够
    if (pkthdr->caplen < 34) return;
    inet_ntop(AF_INET, &(ip_hdr->ip_src), src_ip, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &(ip_hdr->ip_dst), dst_ip, INET_ADDRSTRLEN);
    // 每收到一个包就立即上传
    switch (ip_hdr->ip_p) {
        case IPPROTO_TCP: {
            struct tcphdr *tcp_hdr = (struct tcphdr *)((u_char *)ip_hdr + (ip_hdr->ip_hl << 2));
            printf("[TCP] %s:%d -> %s:%d (%d B)\n", src_ip, ntohs(tcp_hdr->th_sport), dst_ip, ntohs(tcp_hdr->th_dport), pkthdr->len);
            send_packet_to_api("TCP", src_ip, ntohs(tcp_hdr->th_sport), dst_ip, ntohs(tcp_hdr->th_dport), pkthdr->len);
            break;
        }
        case IPPROTO_UDP: {
            struct udphdr *udp_hdr = (struct udphdr *)((u_char *)ip_hdr + (ip_hdr->ip_hl << 2));
            printf("[UDP] %s:%d -> %s:%d (%d B)\n", src_ip, ntohs(udp_hdr->uh_sport), dst_ip, ntohs(udp_hdr->uh_dport), pkthdr->len);
            send_packet_to_api("UDP", src_ip, ntohs(udp_hdr->uh_sport), dst_ip, ntohs(udp_hdr->uh_dport), pkthdr->len);
            break;
        }
        case IPPROTO_ICMP: {
            printf("[ICMP] %s -> %s (%d B)\n", src_ip, dst_ip, pkthdr->len);
            send_packet_to_api("ICMP", src_ip, 0, dst_ip, 0, pkthdr->len);
            break;
        }
        default:
            printf("[OTHER] %s -> %s (%d B)\n", src_ip, dst_ip, pkthdr->len);
            send_packet_to_api("OTHER", src_ip, 0, dst_ip, 0, pkthdr->len);
    }
    // 更新当前时间窗口
    time_t current_time = time(NULL);
    if (current_time != last_print_time) {
        cur_sec = (cur_sec + 1) % MAX_SECONDS;
        stats[cur_sec].bytes = 0;
        stats[cur_sec].pkts = 0;
        last_print_time = current_time;
        
        // 每秒打印一次统计信息
        print_stats();
    }

    // 更新分IP分方向统计
    int src_idx = get_ip_index(src_ip);
    int dst_idx = get_ip_index(dst_ip);
    if (src_idx >= 0) {
        send_stats[src_idx].bytes += pkthdr->len;
        send_stats[src_idx].pkts += 1;
        send_stats[src_idx].total += pkthdr->len;
    }
    if (dst_idx >= 0) {
        recv_stats[dst_idx].bytes += pkthdr->len;
        recv_stats[dst_idx].pkts += 1;
        recv_stats[dst_idx].total += pkthdr->len;
    }

    // 更新全局统计信息
    stats[cur_sec].bytes += pkthdr->len;
    stats[cur_sec].pkts  += 1;
    total += pkthdr->len;
    
    // 更新全局峰值
    if (stats[cur_sec].bytes > peak) {
        peak = stats[cur_sec].bytes;
    }
}

/**
 * 主函数
 * 功能：初始化并启动流量监控
 * 实现原理：
 * 1. 解析命令行参数
 * 2. 初始化网络接口和过滤器
 * 3. 启动数据包捕获循环
 * 4. 处理程序退出
 * 
 * libpcap函数说明：
 * 1. pcap_open_live: 打开网络接口
 *    参数：
 *    - device: 网络接口名称
 *    - snaplen: 捕获的最大字节数
 *    - promisc: 是否启用混杂模式
 *    - to_ms: 超时时间（毫秒）
 *    - errbuf: 错误信息缓冲区
 * 
 * 2. pcap_compile: 编译BPF过滤表达式
 *    参数：
 *    - p: pcap会话句柄
 *    - fp: 编译后的BPF程序
 *    - str: 过滤表达式
 *    - optimize: 是否优化
 *    - netmask: 网络掩码
 * 
 * 3. pcap_setfilter: 设置过滤器
 *    参数：
 *    - p: pcap会话句柄
 *    - fp: 编译后的BPF程序
 * 
 * 4. pcap_dispatch: 捕获并处理数据包
 *    参数：
 *    - p: pcap会话句柄
 *    - cnt: 处理的数据包数量（-1表示无限）
 *    - callback: 回调函数
 *    - user: 用户数据
 * 
 * @param argc 参数个数
 * @param argv 参数数组
 * @return 程序退出状态
 */
int main(int argc, char *argv[]) {
    char *dev;  // 网络接口名称
    char errbuf[PCAP_ERRBUF_SIZE];  // 错误信息缓冲区
    struct bpf_program fp;  // BPF过滤器程序
    bpf_u_int32 net = 0;    // 网络地址
    pcap_t *handle;         // 数据包捕获句柄

    // 检查命令行参数
    if (argc < 2) {
        printf("用法: %s <网络接口> [过滤表达式] [API服务器地址]\n", argv[0]);
        return 1;
    }

    dev = argv[1];  // 获取网络接口名称
    char *filter_exp = (argc > 2) ? argv[2] : "ip";  // 获取过滤表达式，默认为"ip"
    api_url = (argc > 3) ? argv[3] : NULL;  // 获取API服务器基础地址（如 http://172.30.127.8:5000/api）

    // 设置信号处理函数
    signal(SIGINT, handle_sigint);

    // 初始化libcurl
    curl_global_init(CURL_GLOBAL_ALL);

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

    printf("开始捕获 %s 上的流量...\n", dev);
    printf("过滤表达式: %s\n", filter_exp);
    printf("按 Ctrl+C 停止捕获\n\n");

    // 初始化时间
    start_time = time(NULL);  // 记录程序启动时间
    last_print_time = start_time;

    // 主循环：捕获和处理数据包
    while (running) {
        pcap_dispatch(handle, -1, packet_handler, NULL);
    }

    // 清理资源
    pcap_freecode(&fp);
    pcap_close(handle);
    curl_global_cleanup();
    printf("\n\n捕获已停止\n");
    return 0;
}