# Display BLE Protocol v2

## Roles

- ESP32-S3 圆形显示终端：BLE Peripheral / Server
- 激光设备：BLE Central / Client
- 广播短名：`IG_ROUND`
- Notify 周期：最多每 500 ms 一次，即 2 Hz

Display State 是唯一内部状态源。BLE 只读取 Display State、构建数据包并发布，不维护业务状态，也不直接接收 Touch UI 或 Voice Control 的控制调用。

## GATT

| Item | Value |
|---|---|
| Display Service UUID | `ADB402C0-B1C6-11ED-AFA1-0242AC120010` |
| Display State Characteristic UUID | `ADB40201-B1C6-11ED-AFA1-0242AC120011` |
| Characteristic properties | Read + Notify |
| Preferred ATT MTU | 64 |
| Minimum ATT MTU for one packet | 37 |

Server 设置 MTU 64 只是首选值；Laser Client 连接后也必须请求足够大的 MTU。34-byte Notify payload 需要 negotiated ATT MTU 至少为 37。

## Packet Layout

Demo v2.0 使用 34-byte 固定状态包：

```text
Byte 0:  Header = 0xAB
Byte 1:  Payload Length = 0x20
Byte 2..33: CMD + Length + Data fields
```

所有多字节数值使用 little-endian。

| CMD | Length | Data | Unit / rule |
|---:|---:|---|---|
| `0x01` | 1 | projector state | `0` off, `1` on |
| `0x02` | 1 | brightness | `0..100` percent |
| `0x03` | 1 | display battery | `0..100`, `0xFF` unknown |
| `0x04` | 2 | speed | integer m/s, uint16 LE |
| `0x05` | 4 | workout duration | seconds, uint32 LE |
| `0x06` | 2 | calories | kcal, uint16 LE; also carries Demo v2 page-selection semantics |
| `0x07` | 4 | distance | metres, uint32 LE |
| `0x08` | 1 | lap count | uint8 |

Field order is fixed in Protocol v2. The payload bytes add up to `0x20`, and the two-byte packet prefix produces a total length of 34 bytes.

## Demo v2 Page Semantics

Demo v2 removes CMD09 and `projectionImageId`.

- Workout GPS page sends CMD06=`0`; the Laser Client interprets this as Image 62.
- IG Control page sends CMD06=`max(realCalories, 1)`; the Laser Client interprets any nonzero value as Image 63.
- The wire-only value `1` is used when real calories are zero. It does not overwrite Display State calories.
- Other pages preserve the actual calories value without adding a second state source.

This page-selection behavior is a current product contract. The Laser Client must implement the same rule before final end-to-end acceptance.

## Example Shape

```text
AB 20
01 01 <projector>
02 01 <brightness>
03 01 <display_battery>
04 02 <speed_le16>
05 04 <duration_le32>
06 02 <calories_le16>
07 04 <distance_le32>
08 01 <laps>
```

The authoritative implementation is:

- `firmware/esp32_s3_touch_amoled_1_75/ui_static_mock/display_protocol_v2.h`
- `firmware/esp32_s3_touch_amoled_1_75/ui_static_mock/display_protocol_v2.cpp`
- `firmware/esp32_s3_touch_amoled_1_75/ui_static_mock/display_ble_server.cpp`

## Compatibility

Demo v1 used a 37-byte `AB 23` packet and CMD09. That format is historical and is not the final Demo v2 wire contract. Do not mix Demo v1 and Demo v2 client parsers.
