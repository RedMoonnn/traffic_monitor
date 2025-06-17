# 编译器设置
# 使用 OpenWrt 的交叉编译工具链，针对 x86_64 架构，使用 musl libc
# musl libc 是一个轻量级的 C 标准库实现，常用于嵌入式系统
CC = x86_64-openwrt-linux-musl-gcc

# OpenWrt SDK 的路径设置
# staging_dir 是 OpenWrt 编译过程中的中间目录，包含交叉编译工具链和库文件
STAGING_DIR = /home/chy_wsl/openwrt/openwrt-sdk-24.10.0-x86_64_gcc-13.3.0_musl.Linux-x86_64/staging_dir

# 编译选项设置
# -O2: 优化级别2，在代码大小和执行速度之间取得平衡
# -Wall: 启用所有警告信息
# -I: 指定头文件搜索路径，这里指向 OpenWrt 目标系统的头文件目录
CFLAGS = -O2 -Wall -I$(STAGING_DIR)/target-x86_64_musl/usr/include

# 链接选项设置
# -L: 指定库文件搜索路径，这里指向 OpenWrt 目标系统的库文件目录
LDFLAGS = -L$(STAGING_DIR)/target-x86_64_musl/usr/lib

# 需要链接的库文件
# -lpcap: libpcap 库，用于网络数据包捕获
# -lcurl: libcurl 库，用于 HTTP 请求
# -lmbedtls -lmbedx509 -lmbedcrypto: mbedTLS 加密库，用于 HTTPS 支持
# -lnghttp2: HTTP/2 协议支持库
LIBS = -lpcap -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lnghttp2

# 目标可执行文件名
TARGET = traffic_monitor

# 源文件
SRC = traffic_monitor.c

# 默认目标
# 当执行 make 命令时，会首先执行这个目标
all: $(TARGET)

# 编译规则
# $@ 表示目标文件（traffic_monitor）
# $^ 表示所有依赖文件（traffic_monitor.c）
# 使用 @echo 打印编译命令，方便调试
$(TARGET): $(SRC)
	@echo "编译命令: $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)"
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

# 清理规则
# 删除编译生成的可执行文件
clean:
	rm -f $(TARGET)