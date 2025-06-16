# libcurl 库函数使用说明

### 1. 基础函数

#### curl_global_init
```c
CURLcode curl_global_init(long flags)
```
**功能**：初始化全局 curl 环境  
**参数**：
- `flags`: 初始化选项
  - `CURL_GLOBAL_ALL`: 初始化所有功能
  - `CURL_GLOBAL_SSL`: 仅初始化 SSL
  - `CURL_GLOBAL_WIN32`: 仅初始化 Win32 相关功能
  - `CURL_GLOBAL_NOTHING`: 不初始化任何功能  
**返回值**：
- `CURLE_OK`: 成功
- 其他值表示错误  
**使用示例**：
```c
curl_global_init(CURL_GLOBAL_ALL);
```

#### curl_easy_init
```c
CURL *curl_easy_init(void)
```
**功能**：创建新的 curl 句柄  
**返回值**：
- 成功：返回 CURL 句柄
- 失败：返回 NULL  
**使用示例**：
```c
CURL *curl = curl_easy_init();
if (!curl) {
    // 处理错误
}
```

#### curl_easy_cleanup
```c
void curl_easy_cleanup(CURL *curl)
```
**功能**：清理 curl 句柄  
**参数**：
- `curl`: 要清理的 curl 句柄  
**使用示例**：
```c
curl_easy_cleanup(curl);
```

### 2. 请求配置函数

#### curl_easy_setopt
```c
CURLcode curl_easy_setopt(CURL *curl, CURLoption option, ...)
```
**功能**：设置 curl 请求选项  
**常用选项**：
1. 基本选项：
```c
// 设置请求URL
curl_easy_setopt(curl, CURLOPT_URL, "http://example.com");

// 设置请求方法
curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);    // GET请求
curl_easy_setopt(curl, CURLOPT_POST, 1L);       // POST请求
curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data); // POST数据

// 设置超时
curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);           // 整体超时时间
curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);     // 连接超时时间
```

2. 请求头相关：
```c
// 设置请求头
struct curl_slist *headers = NULL;
headers = curl_slist_append(headers, "Content-Type: application/json");
curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
```

3. 响应处理：
```c
// 设置响应回调函数
curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

// 设置错误处理
curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);            // 启用详细输出
curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer); // 错误信息缓冲区
```

### 3. 请求执行函数

#### curl_easy_perform
```c
CURLcode curl_easy_perform(CURL *curl)
```
**功能**：执行 HTTP 请求  
**参数**：
- `curl`: curl 句柄  
**返回值**：
- `CURLE_OK`: 成功
- 其他值表示错误  
**使用示例**：
```c
CURLcode res = curl_easy_perform(curl);
if (res != CURLE_OK) {
    // 处理错误
}
```

### 4. 响应处理函数

#### 回调函数格式
```c
size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata)
```
**功能**：处理 HTTP 响应数据  
**参数**：
- `ptr`: 响应数据指针
- `size`: 单个数据块大小
- `nmemb`: 数据块数量
- `userdata`: 用户自定义参数  
**返回值**：实际处理的数据字节数  

**使用示例**：
```c
// 1. 丢弃响应（如我们的代码中使用的）
size_t discard_response(void *ptr, size_t size, size_t nmemb, void *userdata) {
    return size * nmemb;  // 返回处理的数据大小，但不做任何处理
}

// 2. 保存响应到字符串
struct string {
    char *ptr;
    size_t len;
};

void init_string(struct string *s) {
    s->len = 0;
    s->ptr = malloc(s->len + 1);
    s->ptr[0] = '\0';
}

size_t write_response(void *ptr, size_t size, size_t nmemb, struct string *s) {
    size_t new_len = s->len + size * nmemb;
    s->ptr = realloc(s->ptr, new_len + 1);
    memcpy(s->ptr + s->len, ptr, size * nmemb);
    s->ptr[new_len] = '\0';
    s->len = new_len;
    return size * nmemb;
}
```

### 5. 完整使用示例

#### 发送 JSON 数据的 POST 请求
```c
void send_json_post(const char *url, const char *json_data) {
    CURL *curl = curl_easy_init();
    if (!curl) return;

    // 设置请求头
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    // 设置选项
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_response);
    
    // 执行请求
    curl_easy_perform(curl);
    
    // 清理
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}
```

#### 带错误处理的 GET 请求
```c
void safe_get_request(const char *url) {
    CURL *curl = curl_easy_init();
    if (!curl) return;

    char error_buffer[CURL_ERROR_SIZE];
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    
    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        printf("请求失败: %s\n", error_buffer);
    }
    
    curl_easy_cleanup(curl);
}
```

### 6. 在我们的代码中的应用

在我们的流量监控程序中，主要使用 libcurl 进行以下操作：

1. 发送全局统计数据：
```c
void send_stats_to_api(unsigned long total, unsigned long peak, float avg2, float avg10, float avg40) {
    if (!api_url) return;
    
    CURL *curl = curl_easy_init();
    if (!curl) return;
    
    // 设置丢弃响应的回调
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_response);
    
    // 构建URL和JSON数据
    char url[512];
    snprintf(url, sizeof(url), "%s/update", api_url);
    
    char json_data[256];
    snprintf(json_data, sizeof(json_data),
             "{\"total\":%lu,\"peak\":%lu,\"avg2\":%.1f,\"avg10\":%.1f,\"avg40\":%.1f}",
             total, peak, avg2, avg10, avg40);
    
    // 设置请求头
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    
    // 设置请求选项
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    // 执行请求
    curl_easy_perform(curl);
    
    // 清理资源
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
}
```

2. 发送数据包信息：
```c
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
```

这些函数使得我们的程序能够方便地与后端 API 进行通信，实现流量数据的实时上报和可视化。 