CC = x86_64-openwrt-linux-musl-gcc
STAGING_DIR = /home/chy_wsl/openwrt/openwrt-sdk-24.10.0-x86-64_gcc-13.3.0_musl.Linux-x86_64/staging_dir
CFLAGS = -O2 -Wall -I$(STAGING_DIR)/target-x86_64_musl/usr/include
LDFLAGS = -L$(STAGING_DIR)/target-x86_64_musl/usr/lib
LIBS = -lpcap -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lnghttp2

TARGET = traffic_monitor
SRC = traffic_monitor.c

all: $(TARGET)

$(TARGET): $(SRC)
	@echo "编译命令: $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)"
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

clean:
	rm -f $(TARGET)