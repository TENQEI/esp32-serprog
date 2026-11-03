# 上游审计与实现边界

## 来源

检查时 TENQEI/esp32-serprog 与 thisiseth/esp32-serprog 的 main 均为
0ca3c7e6d3099ed08b1aaaab80a4f3af64d97275。
保留 COPYING（GPLv3）和 serprog 协议实现，不修改任何小智仓库。

上游实际工程位置为 src/serprog：
- README 要求 ESP-IDF v6.0 及以上，代码使用 ESP-IDF 6 的 VFS/USB Serial/JTAG 驱动。
- 原先非经典 ESP32 分支默认 MISO=2、MOSI=41、CLK=40、CS=42。
- 原先启动即初始化 SPI，初始 8 MHz，允许主机请求任意频率。
- 原先 O_SPIOP 没有 opcode 白名单，能透传写入和擦除事务。
- 原先超长 TX 帧拒绝时没有排空 payload，可能使剩余字节成为协议命令。
- 原先 KConfig.projbuild 大小写不符合标准命名，改为 Kconfig.projbuild。

本分支固定 ESP32-S3、原生 USB 和 SPI2 10/11/12/13；Wi-Fi 或其他目标配置导致编译失败。
复用 serprog 协议协商和底层 SPI 传输，新增独立 readonly_policy.h。
PSRAM 不启用；16 MB 是 ESP 自身模块的存储配置，与被读取 Flash 容量检查相互独立。

## 固件白名单

| opcode | 事务 TX 字节数 | RX 字节数 | 用途 |
| --- | --- | --- | --- |
| 9F | 1 | 1–3 | JEDEC ID |
| 05 / 35 / 15 | 1 | 1 | 状态寄存器 1/2/3 |
| 03 | 4 | 1–8192 | 24 位地址普通读取 |
| 0B | 5 | 1–8192 | 24 位地址快速读取，含 dummy 字节 |
| 5A | 5 | 1–8192 | SFDP，含 dummy 字节 |

每个事务先完整收 TX、校验边界/白名单/物理解锁/输出使能，再申请 SPI。
TX/RX 单边不得超过 8192，总缓存 16384。数据读取不得越过 0xFFFFFF。
被拒绝的事务不会产生 SPI 时钟；超长帧排空声明的 TX payload 后 NAK，
防止将 payload 中的 03 等字节当作新的 serprog 命令。
若恶意主机声明长帧却不发送，固件会等待数据；可复位恢复，期间不会把该帧送到 SPI。
不将 USB 连接视为可信，也不提供“暂时关闭只读”的开关。

全部未列 opcode 默认拒绝，包括 WREN 06、volatile WREN 50、WRSR 01/31/11、
PP 02/32、erase 20/52/D8/60/C7、OTP 写/擦、复位 66/99、掉电 B9、唤醒 AB、
QPI/连续读取模式及四字节地址切换。AB 虽能返回器件 ID，也改变掉电状态，故不放行。
普通 SPI 无法读取处于 QPI/特殊模式的器件时，应先解决电气隔离与已知上电状态，
不能添加通用模式修改命令。

## 输出与频率

GPIO0（BOOT）上电后须先释放再连续按住至少两秒，独立任务才设置原子解锁标记。
标记保持到 ESP 复位，正常关闭的两次读取无需重复按键。
S_PIN_STATE=1 还需此标记；S_PIN_STATE=0 释放 SPI 外设、关闭输出和内部上下拉。
这不是电气互锁：固件没有测 VCC/电流/STM32 状态，不会给目标供电。
异常断连不能保证自动退出，应使用物理隔离和受控断电。

频率请求不初始化尚未使用的 SPI。首次合法事务才配置外设；
初始请求 500 kHz，硬上限默认 500 kHz。使用 80 MHz APB 下的
100 kHz、250 kHz、500 kHz、1 MHz、2 MHz、4 MHz 离散档，向下选择，不超过请求/配置上限。
小于 100 kHz 拒绝。初始化后再次核对驱动返回的实际频率，超过选择值就释放并拒绝。
提高上限需重新构建；主机不能突破 CONFIG_READER_MAX_SPI_HZ。

## 参考资料

- [上游源码](https://github.com/thisiseth/esp32-serprog/tree/0ca3c7e6d3099ed08b1aaaab80a4f3af64d97275)
- [ESP-IDF v6.0](https://github.com/espressif/esp-idf/tree/v6.0)
- [ESP32-S3 原生 USB Serial/JTAG](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32s3/api-guides/usb-serial-jtag-console.html)
- [Winbond W25Q128JV 文档入口](https://www.winbond.com/hq/support/documentation/?__locale=en&category=%2F.categories%2Fresources%2Fdatasheet%2F&family=%2Fproduct%2Fcode-storage-flash-memory%2Fserial-nor-flash%2Findex.html&line=%2Fproduct%2Fcode-storage-flash-memory%2Findex.html&pno=W25Q128JV)
- [flashrom serprog 实现](https://github.com/flashrom/flashrom/blob/main/programmers/serprog.c)
- [flashrom Winbond 型号映射](https://github.com/flashrom/flashrom/blob/main/flashchips/winbond.c)
