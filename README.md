# IG Daylight Display

基于 ESP32-S3 的圆形 AMOLED 骑行终端，用于本地触控交互、运动数据展示，并通过 BLE 与外部 IG 激光显示设备同步状态。本仓库是面向简历与技术面试的精简展示版，仅保留最终集成工程、核心业务代码和必要技术说明。

> 项目来源于实习团队项目。仓库重点展示嵌入式软件集成、LVGL 界面、状态管理、I2C 外设接入和 BLE 通信；语音命令方案并非本人独立设计。

## 核心功能

- 466 × 466 圆形 AMOLED 界面，基于 LVGL 8.4 实现五个业务页面与全局电量环
- 触摸页面切换、投影开关、亮度调节及骑行 Start / Pause / Resume
- 统一 `Display State` 管理界面、触摸、传感器和 BLE 之间的业务状态
- BLE Peripheral / Server，以 2 Hz 频率发布 34 字节 Protocol v2 状态包
- LC76G GPS 数据解析、骑行计时、距离和估算卡路里计算
- GPS 速度失效时使用 QMI8658 提供固定安装场景的速度估算回退
- AXP2101 电源管理、PCF85063 RTC、CST92xx 触摸等多设备共享 I2C 总线
- 集成 ESP-SR 语音识别适配与 SD 卡 WAV 反馈播放链路

## 系统架构

```text
Touch UI -----------+
Voice Adapter ------+----> Display State ----> LVGL UI
Workout Engine -----+             |
GPS / IMU ----------+             +----------> BLE Packet Builder
PMU / RTC ----------+                              |
                                                     v
                                              IG Laser Client
```

`Display State` 是唯一业务状态源。触摸与语音适配层只通过 setter 修改状态；GPS、IMU 和 Workout Engine 更新运动数据；BLE 读取同一份状态并构建数据包，不直接操作 LVGL 对象。

## 硬件平台

| 模块 | 型号 / 配置 |
|---|---|
| 主控 | ESP32-S3R8，16 MB Flash，8 MB Octal PSRAM |
| 显示 | 1.75 英寸 466 × 466 AMOLED，CO5300 |
| 触摸 | CST9217 / CST92xx |
| 定位 | LC76G GPS |
| IMU | QMI8658 |
| 电源管理 | AXP2101 |
| RTC | PCF85063 |
| 音频 | ES7210 输入、ES8311 输出、板载功放与扬声器 |
| 存储 | MicroSD / SDMMC |

I2C 使用 Arduino-ESP32 Wire NG，由一个 owner 在 GPIO15 / GPIO14、100 kHz 下统一初始化，Touch、PMU、RTC、GPS 和 IMU 共用该总线，避免重复初始化和新旧驱动冲突。详细地址与接口见 [硬件说明](docs/HARDWARE.md)。

## BLE 协议

- 广播名称：`IG_ROUND`
- 设备角色：BLE Peripheral / Server
- Characteristic：Read + Notify
- 更新周期：最大 2 Hz
- 数据格式：固定 34 字节，包头 `AB 20`
- 字段：投影状态、亮度、电量、速度、运动时长、卡路里、距离和圈数

完整 UUID、字段偏移和大小端规则见 [Protocol v2](docs/PROTOCOL.md)。

## 软件栈

| 组件 | 版本 |
|---|---|
| ESP-IDF | 5.4.x |
| Arduino-ESP32 Component | 3.3.10 |
| LVGL | 8.4.0 |
| esp-nimble-cpp | 2.5.0 |
| ESP-SR | 2.4.6 |
| esp-dsp | 1.8.0 |
| esp_codec_dev | 1.5.11 |

工程采用 ESP-IDF + Arduino Component 混合架构，通过 `app_main()` 完成 Arduino运行时、显示、BLE和语音模块初始化。

## 目录结构

```text
docs/
  HARDWARE.md                         硬件与总线说明
  PROTOCOL.md                         BLE Protocol v2
firmware/
  esp32_s3_touch_amoled_1_75/
    ui_static_mock/                   LVGL、状态、BLE、GPS/IMU与电源核心代码
integration_tests/
  idf_arduino_hybrid_full_spike/      最终 ESP-IDF 集成工程与主程序入口
```

开发阶段的归档、旧屏幕原型、React UI 原型、构建产物、发布记录和 TODO 均未放入本展示仓库。

## 编译

建议将仓库放在纯英文路径，并安装 ESP-IDF 5.4.x。Arduino_GFX、SensorLib 和 XPowersLib 默认从 `%USERPROFILE%/Documents/Arduino/libraries` 接入，也可通过 `IG_ARDUINO_LIB_ROOT` 指定。

```powershell
cd integration_tests/idf_arduino_hybrid_full_spike
idf.py set-target esp32s3
idf.py reconfigure
idf.py build
```

连接开发板并确认实际串口后烧录：

```powershell
idf.py -p COMx flash monitor
```

## 项目展示重点

- LVGL 显示刷新、页面组件与触摸事件处理
- 多模块共享 I2C 总线及外设数据采集
- BLE GATT Server、Notify 频率控制和二进制协议封包
- GPS NMEA 解析、IMU 回退及运动数据计算
- ESP-IDF / Arduino混合工程集成与软硬件联调

当前代码用于项目能力展示；实际硬件型号、引脚和依赖版本以源码及 `sdkconfig.defaults` 为准。
