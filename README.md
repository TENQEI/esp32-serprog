# ESP32-S3 N16R8 SPI NOR 只读器

基于 [thisiseth/esp32-serprog](https://github.com/thisiseth/esp32-serprog)，
为 TENQEI 的双 USB-C / CH343P ESP32-S3 N16R8 开发板配置。
仅使用原生 USB Serial/JTAG 和 SPI2，读取在板 Winbond W25Q128JV（16 MiB）。
独立项目，不依赖或修改小智语音终端。

**先看 [在板读取电气安全检查](docs/ELECTRICAL_SAFETY.zh-CN.md)。未确认供电轨与 STM32 总线状态，
不要接入 SPI 信号，不要给目标板供电，不要按 BOOT 解锁。固件不能检测这些条件。**

## 功能

- 固件端默认拒绝，仅允许 9F、05、35、15、03、0B、5A 的严格长度读取事务。
- 写使能、编程、擦除、修改状态、复位、掉电唤醒、QPI/地址模式修改均不支持。
- GPIO10/11/12/13 上电为输入、无内部上下拉；不初始化 SPI 外设。
- 上电后先松开 BOOT，再按住两秒完成物理解锁；主机不能替代这个动作。
- serprog 输出使能后，只有通过白名单的读取才初始化 SPI。
- 初始时钟 500 kHz，默认硬上限 500 kHz。提高上限必须重新配置并编译。
- 主机正常关闭时通过 serprog 输出禁用释放 SPI；异常退出后应断开测试夹、复位开发板。
- 不控制目标 VCC；不提供自动供电功能。

## 文档与脚本

| 内容 | 入口 |
| --- | --- |
| 电气检查、供电、STM32 总线隔离 | [ELECTRICAL_SAFETY.zh-CN.md](docs/ELECTRICAL_SAFETY.zh-CN.md) |
| 接线表 | [WIRING.zh-CN.md](docs/WIRING.zh-CN.md) |
| Apple Silicon Mac 安装、编译、烧录与读取 | [MACOS.zh-CN.md](docs/MACOS.zh-CN.md) |
| 故障排查 | [TROUBLESHOOTING.zh-CN.md](docs/TROUBLESHOOTING.zh-CN.md) |
| 上游审计、白名单、安全边界 | [IMPLEMENTATION.zh-CN.md](docs/IMPLEMENTATION.zh-CN.md) |
| 编译与烧录 | scripts/build_macos.sh、scripts/flash_macos.sh |
| 两次完整读取 | scripts/read_twice.py |
| 独立文件校验 | scripts/verify_dumps.py |

项目目录为 `src/serprog`；固定 ESP-IDF v6.0.x，CI 使用 v6.0。
N16 配置为 16 MB DIO / 40 MHz，R8 PSRAM 不启用。

## 许可证与来源

保留上游 GPLv3 许可证全文 [COPYING](COPYING)。原始实现来自 thisiseth/esp32-serprog，
其 README 说明复用了 stacksmashing/pico-serprog 的片段，并参考 stm32-vserprog。
分发编译固件时同时提供对应源码与 GPLv3 许可信息。源代码审计不等于实物电气验证。
