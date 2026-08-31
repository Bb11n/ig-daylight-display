# Hardware Reference

## 产品平台

最终产品硬件为 Waveshare ESP32-S3-Touch-AMOLED-1.75-G（GPS 版本），不是早期 172 x 320 或 240 x 320 长条屏原型。

| 项目 | 配置 |
|---|---|
| MCU | ESP32-S3R8 |
| Flash | 16 MB |
| PSRAM | 8 MB Octal PSRAM，40 MHz |
| Display | 1.75 英寸 466 x 466 圆形 AMOLED |
| Display controller | CO5300 |
| Touch | CST9217 / CST92xx |
| GPS | LC76G |
| IMU | QMI8658 |
| PMU | AXP2101 |
| RTC | PCF85063 |
| Audio input | ES7210 microphone codec |
| Audio output | ES8311 speaker codec + PA |
| Storage | MicroSD / SDMMC |

## Shared I2C

产品固件统一使用 Arduino-ESP32 3.3.10 的 Wire NG：

```text
SDA = GPIO15
SCL = GPIO14
frequency = 100 kHz
```

总线由一个 owner 初始化一次。Touch、PMU、RTC、GPS 和 QMI8658 的实际事务共享同一条总线与同步边界，不允许模块各自重复 `Wire.begin()`，也不允许重新引入 Legacy `driver/i2c.h` 产品路径。

已知设备：

| 设备 | 地址/协议 |
|---|---|
| CST92xx Touch | `0x5A` |
| AXP2101 PMU | `0x34` |
| PCF85063 RTC | `0x51` |
| QMI8658 IMU | `0x6B` |
| LC76G command write | `0x50` |
| LC76G block read | `0x54` |

LC76G 的 NMEA 数据长度使用 4-byte little-endian 协议。当前 transport 保留完整 block 读取和一次轻量 retry；GNSS FIX 稳定性仍是待验证项。

## Display And Touch

CO5300 由 Arduino_GFX 适配层驱动，LVGL 使用 16-bit color 和单 draw buffer。单 buffer 约 108,578 bytes，用于降低 BLE、ESP-SR 和 LVGL 共存时的内部 SRAM 压力。

显示与触摸引脚、复位和电源时序由板级配置集中维护。不要在页面代码中复制引脚常量，也不要把早期 ST7789 原型参数用于本板。

## Audio

语音输入使用 ES7210，反馈输出使用 ES8311、I2S TX 和 PA。音频路径共用单一 I2S0 owner，SD 卡上的反馈文件使用 FatFs 长文件名支持：

```text
/sdcard/voice/projector_on.wav
/sdcard/voice/projector_off.wav
/sdcard/voice/brightness.wav
```

WAV 基线为 16 kHz、16-bit、mono。不要在没有硬件回归测试的情况下改变 I2S 引脚、codec 配置或 PA 时序。

## Power

电池、USB 和系统电源状态由 AXP2101 提供。PWR 键长按进入 Soft Power 状态机；短按不切换页面或屏幕。BOOT 键保留给烧录和调试。硬件 PMU 的强制关机机制仍作为兜底。

USB 连接时，设备可能在电池电源关闭后仍由 USB 供电，因此不能只凭屏幕或串口是否继续输出判断电池轨是否已经关闭。

## Build Configuration

产品构建固定：

- target：ESP32-S3
- Flash：16 MB
- PSRAM：Octal，40 MHz
- FreeRTOS tick：1000 Hz
- LVGL color depth：16
- NimBLE preferred MTU：64
- FatFs LFN：heap allocation，maximum 64 characters，prefer external RAM

准确配置以 `integration_tests/idf_arduino_hybrid_full_spike/sdkconfig.defaults` 为准。
