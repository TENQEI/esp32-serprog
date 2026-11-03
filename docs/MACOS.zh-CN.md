# Apple Silicon Mac 操作说明

## 1. 安装工具

以下操作仅针对 esp32-serprog，不使用小智仓库或其构建目录。
先使用 Homebrew 安装 git、cmake、ninja、dfu-util、python3、flashrom。
ESP-IDF 使用官方 v6.0.x；示例固定到 v6.0：

```bash
brew install git cmake ninja dfu-util python flashrom
mkdir -p ~/esp
git clone --branch v6.0 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v6.0
cd ~/esp/esp-idf-v6.0
./install.sh esp32s3
source ./export.sh
```

以后每个新终端先 `source ~/esp/esp-idf-v6.0/export.sh`。
先检查目标目录不存在，避免覆盖已有 ESP-IDF 安装。

拉取本仓库的只读分支，进入仓库根目录。若已有克隆，先确认本地修改已妥善保存，再切换分支。

```bash
git clone --branch feat/s3-n16r8-readonly https://github.com/TENQEI/esp32-serprog.git
cd esp32-serprog
bash scripts/build_macos.sh
```

构建在 src/serprog/build-reader，配置在 sdkconfig.reader；每次运行 build_macos.sh
重新从只读默认值配置，500 kHz 上限。R8 PSRAM 不使用。

## 2. 只烧 ESP，不接目标

断开测试夹与目标板的全部接线，再把数据 USB 线接入开发板原生 USB-C。
CH343P 端口不是这份固件的运行端口，无需为原生 USB 安装 CH343 驱动。

```bash
ls /dev/cu.usbmodem*
bash scripts/flash_macos.sh /dev/cu.usbmodemXXXX
```

脚本要求输入 FLASH-ESP32-ONLY。烧录修改的是 ESP 模块内部 Flash，和目标玩具芯片不同。
不要抄用网上的通用偏移烧单个 main.bin；应由本次构建的 idf.py flash 使用正确分区和偏移。
首次无法自动进入下载模式：在完全断开目标的情况下，按住 BOOT，短按 RESET，再松开 BOOT，
重试烧录。烧好后松开 BOOT，再复位进入应用；端口编号可能变化。

## 3. 电气检查、物理解锁和识别

严格按 ELECTRICAL_SAFETY.zh-CN.md 完成记录。断电接线、按验证过的方案上电，
应用启动且 BOOT 已松开后，再按住 BOOT 两秒并松开。
这个动作只表示你已完成检查，固件没有电压或总线状态检测能力。

```bash
flashrom --version
flashrom -p serprog:dev=/dev/cu.usbmodemXXXX:921600,spispeed=500k -VV
```

典型 EF 40 18 对应 3.3 V 的 128 Mbit Winbond 系列，flashrom 可能显示
W25Q128.V，而非完整丝印 W25Q128JV。若识别多个型号，先核对实物后使用准确的 -c 名称；
不同 JV 后缀可能采用不同 ID/名称。不要用 -f 强行匹配或选择 1.8 V 的 W 型号。
原生 USB 的 921600 是 serprog 串口参数，不是实际 USB 吞吐限制。

## 4. 连读两次并校验

下面示例假设已确认 flashrom 的型号名是 W25Q128.V：

```bash
python3 scripts/read_twice.py \
  --port /dev/cu.usbmodemXXXX \
  --chip W25Q128.V \
  --safety-checked \
  --output dumps/flower-500k-01
```

脚本只执行 -r。输出目录必须不存在，不会覆盖已有结果。
成功输出 read1.bin、read2.bin、各自日志、session.txt、SHA256SUMS 和 VERIFIED.txt。
中断/失败保留 .partial 和日志；没有 VERIFIED.txt 就不算通过。
每次严格检查 16,777,216 字节，之后逐字节比较；全 00/FF 即使相同也失败。
两次相同说明重复性通过，仍不能单凭此排除错误接线造成的固定模式。

```bash
python3 scripts/verify_dumps.py dumps/flower-500k-01/read1.bin dumps/flower-500k-01/read2.bin
cd dumps/flower-500k-01
shasum -a 256 -c SHA256SUMS
cmp read1.bin read2.bin
```

500 kHz 的 16 MiB 理论纯数据时间约 268 秒/次，USB、命令及夹线会增加耗时；
两次至少约九分钟，不要以短暂没有终端输出判断卡死，观察日志和文件增长。
数据只保存在 Mac 文件系统，不会先缓存到 ESP 的 N16/R8 存储。

## 5. 稳定后逐步提速

先保留成功的 500 kHz 两份文件和日志。每次只升一级（1 MHz → 2 MHz → 4 MHz），
重新做两次全量读取，并与 500 kHz 的基准逐字节比较。不通过就退回上一个可靠档。

```bash
cd src/serprog
idf.py -B build-reader -D SDKCONFIG=sdkconfig.reader menuconfig
# Read-only SPI NOR reader -> SPI frequency ceiling，先设 1000000
idf.py -B build-reader -D SDKCONFIG=sdkconfig.reader build
python3 ../../scripts/check_config.py sdkconfig.reader
cd ../..
# 断开目标后重刷；此脚本不重置 menuconfig
bash scripts/flash_macos.sh /dev/cu.usbmodemXXXX
# 重新按电气流程连接、上电、物理解锁
python3 scripts/read_twice.py --port /dev/cu.usbmodemXXXX --chip W25Q128.V \
  --speed 1M --baseline dumps/flower-500k-01/read1.bin \
  --safety-checked --output dumps/flower-1M-01
```

默认固件会将高频请求钳制到 500 kHz；只改 flashrom spispeed 不会突破固件上限。
实际频率以 -VV 日志返回值为准；只有确认实际升频后才称为该档位验证。
更高上限固件的首次初始化仍默认 500 kHz，主机可明确请求经过批准的更高档。
不要通过修改 SPI 白名单解决探测失败。
