/*
 * traffic_monitor.c - OpenWrt流量监控采集程序
 * 主要功能：实时捕获网络数据包，统计并上报流量信息
 * 适合初学者学习网络抓包、流量统计、C语言与HTTP交互等知识
 */

#include <pcap.h>           // 用于网络数据包捕获的主库
#include <stdio.h>          // 标准输入输出
#include <stdlib.h>         // 标准库函数
#include <string.h>         // 字符串处理
#include <signal.h>         // 信号处理
#include <netinet/ip.h>     // IP协议头结构体
#include <netinet/tcp.h>    // TCP协议头结构体
#include <netinet/udp.h>    // UDP协议头结构体
#include <netinet/icmp6.h>  // ICMP协议头结构体
#include <arpa/inet.h>      // IP地址转换
#include <time.h>           // 时间相关函数
#include <curl/curl.h>      // HTTP请求库

// ===================== 配置与全局变量 =====================
#define MAX_SECONDS 40   // 统计窗口大小（秒）
#define MAX_IP 100       // 支持最多统计的IP数量

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

Stat stats[MAX_SECONDS] = {0};  // 全局每秒统计环形缓冲区
unsigned long peak = 0;         // 全局峰值速率
unsigned long total = 0;        // 全局累计流量
int cur_sec = 0;                // 当前秒的索引
static volatile sig_atomic_t running = 1;  // 程序运行标志

// 时间相关全局变量
char *api_url = NULL;           // 后端API地址
char ip_list[MAX_IP][INET_ADDRSTRLEN]; // 记录所有出现过的IP
int ip_count = 0;               // 当前IP数量
PerIPStat send_stats[MAX_IP] = {0}; // 每个IP的发送统计
PerIPStat recv_stats[MAX_IP] = {0}; // 每个IP的接收统计

time_t last_print_time = 0;     // 上次统计时间

// ========== 工具函数 ==========
/**
 * 丢弃HTTP响应内容的回调，防止libcurl输出内容到终端
 * @param ptr   数据指针
 * @param size  单个数据块大小
 * @param nmemb 数据块数量
 * @param userdata 用户自定义参数（未用）
 * @return 实际处理的数据字节数
 */
size_t discard_response(void *ptr, size_t size, size_t nmemb, void *userdata) {
    return size * nmemb;
}

// ========== 信号处理 ==========
/**
 * Ctrl+C信号处理，优雅退出
 * 第一次Ctrl+C：设置running=0，允许主循环退出
 * 再次Ctrl+C：强制退出
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

// ========== HTTP上报相关 ==========
/**
 * 发送全局统计数据到后端API
 * @param total 累计总流量
 * @param peak  峰值速率
 * @param avg2/avg10/avg40 不同窗口平均速率
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
 * 发送单个数据包信息到后端API（可用于抓包日志分析）
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
 * 获取或创建IP索引
 * 若IP已存在，返回其索引；否则新建并初始化
 */
int get_ip_index(const char *ip) {
    for (int i = 0; i < ip_count; i++) {
        if (strcmp(ip_list[i], ip) == 0) return i;
    }
    if (ip_count < MAX_IP) {
        strncpy(ip_list[ip_count], ip, INET_ADDRSTRLEN - 1);
        ip_list[ip_count][INET_ADDRSTRLEN - 1] = '\0';
        memset(send_stats[ip_count].bytes_history, 0, sizeof(send_stats[ip_count].bytes_history));
        memset(recv_stats[ip_count].bytes_history, 0, sizeof(recv_stats[ip_count].bytes_history));
        send_stats[ip_count].history_idx = -1;
        recv_stats[ip_count].history_idx = -1;
        return ip_count++;
    }
    return -1;
}

/**
 * 发送所有IP分方向统计数据到后端API
 * 组装为JSON数组，便于前端批量展示
 */
void send_per_ip_stats_to_api() {
    if (!api_url) return;
    CURL *curl = curl_easy_init();
    if (!curl) return;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_response);
    char url[512];
    snprintf(url, sizeof(url), "%s/update", api_url);
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
 * 统计每个IP分方向40秒窗口内的峰值速率
 * 每秒推进环形缓冲区，取最大值
 */
void update_per_ip_peak() {
    for (int i = 0; i < ip_count; i++) {
        send_stats[i].history_idx = (send_stats[i].history_idx + 1) % MAX_SECONDS;
        send_stats[i].bytes_history[send_stats[i].history_idx] = send_stats[i].bytes;
        recv_stats[i].history_idx = (recv_stats[i].history_idx + 1) % MAX_SECONDS;
        recv_stats[i].bytes_history[recv_stats[i].history_idx] = recv_stats[i].bytes;
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
 * 每秒打印并上报统计信息
 * 计算不同窗口平均速率，推送全局和分IP统计
 */
void print_stats() {
    unsigned long sum2 = 0, sum10 = 0, sum40 = 0;
    int count2 = 0, count10 = 0, count40 = 0;
    time_t current_time = time(NULL);
    time_t elapsed = current_time - start_time;
    update_per_ip_peak();
    for (int i = 0; i < 2 && i < MAX_SECONDS; ++i) {
        sum2 += stats[(cur_sec - i + MAX_SECONDS) % MAX_SECONDS].bytes;
        count2++;
    }
    for (int i = 0; i < 10 && i < MAX_SECONDS; ++i) {
        sum10 += stats[(cur_sec - i + MAX_SECONDS) % MAX_SECONDS].bytes;
        count10++;
    }
    for (int i = 0; i < 40 && i < MAX_SECONDS; ++i) {
        sum40 += stats[(cur_sec - i + MAX_SECONDS) % MAX_SECONDS].bytes;
        count40++;
    }
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
    for (int i = 0; i < ip_count; i++) {
        send_stats[i].avg2 = avg2;
        send_stats[i].avg10 = avg10;
        send_stats[i].avg40 = avg40;
        recv_stats[i].avg2 = avg2;
        recv_stats[i].avg10 = avg10;
        recv_stats[i].avg40 = avg40;
    }
    send_stats_to_api(total, peak, avg2, avg10, avg40);
    send_per_ip_stats_to_api();
    for (int i = 0; i < ip_count; i++) {
        send_stats[i].bytes = 0;
        recv_stats[i].bytes = 0;
    }
}

/**
 * 数据包处理函数（主回调）
 * 每收到一个包，解析协议、统计流量、上报包信息
 */
void packet_handler(u_char *user_data, const struct pcap_pkthdr *pkthdr, const u_char *packet) {
    struct ip *ip_hdr = (struct ip *)(packet + 14); // 跳过以太网头
    char src_ip[INET_ADDRSTRLEN], dst_ip[INET_ADDRSTRLEN];
    if (pkthdr->caplen < 34) return; // 长度校验
    inet_ntop(AF_INET, &(ip_hdr->ip_src), src_ip, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &(ip_hdr->ip_dst), dst_ip, INET_ADDRSTRLEN);
    // 协议分支，打印并上报包信息
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
    // 判断是否进入新的一秒，若是则统计并上报
    time_t current_time = time(NULL);
    if (current_time != last_print_time) {
        cur_sec = (cur_sec + 1) % MAX_SECONDS;
        stats[cur_sec].bytes = 0;
        stats[cur_sec].pkts = 0;
        last_print_time = current_time;
        print_stats();
    }
    // 分IP分方向统计累加
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
    // 全局统计
    stats[cur_sec].bytes += pkthdr->len;
    stats[cur_sec].pkts  += 1;
    total += pkthdr->len;
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