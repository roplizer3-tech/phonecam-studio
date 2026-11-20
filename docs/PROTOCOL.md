# PhoneCam Studio Wire Protocol (v1)

## Ports & Discovery
- **TCP Stream & Control:** `54820`
- **UDP Discovery Beacon:** `54821`

## 16-Byte Binary Header (Little-Endian)
| Offset | Size | Field | Description |
|---|---|---|---|
| `0` | `4` | `magic` | `0x5043414D` (`'P','C','A','M'` LE) |
| `4` | `1` | `type` | Packet type enum (`1..8`) |
| `5` | `1` | `flags` | Bitmask (`KEYFRAME`, `CODEC_CONFIG`, `FRONT_CAMERA`, `ROTATION_CODE`, `MIRRORED_BY_SENSOR`) |
| `6` | `2` | `reserved` | `0` |
| `8` | `4` | `payloadSize` | Payload bytes (`0` .. `8388608` = 8 MiB max) |
| `12` | `4` | `timestampMs` | Sender monotonic millisecond clock |

### Packet Types (`type`)
- `1` (`HELLO`): Phone -> PC UTF-8 JSON with `deviceName`, `protocolVersion`, `pinRequired`, `nonce` (32 hex chars), `cameras[]`.
- `2` (`AUTH`):
  - PC -> Phone JSON: `{"pinHash": "<lowercase_sha256_hex(nonce + ':' + pin)>"}`
  - Phone -> PC JSON: `{"ok": true|false, "reason": "..."}`
- `3` (`VIDEO_H264`): Raw Annex-B NAL unit(s). SPS/PPS packets set `CODEC_CONFIG` (`0x02`).
- `4` (`VIDEO_MJPEG`): Complete JPEG frame.
- `5` (`CONTROL`): Bidirectional UTF-8 JSON commands:
  - `{"cmd":"switch_camera","cameraId":"...","facing":"front"|"back"}`
  - `{"cmd":"set_config","width":1280,"height":720,"fps":30,"bitrate":4000000,"codec":"h264"|"mjpeg"}`
  - `{"cmd":"set_torch","enabled":true|false}`
  - `{"cmd":"tap_focus","nx":0.5,"ny":0.5}`
  - `{"cmd":"set_zoom","ratio":1.5}`
  - `{"cmd":"request_keyframe"}`
- `6` (`TELEMETRY`): Phone -> PC UTF-8 JSON (`cameraId`, `facing`, `sensorOrientation`, `deviceRotation`, `width`, `height`, `fps`, `batteryPct`, `tempC`, `torchOn`, `codec`).
- `7` (`PING`): PC -> Phone `8`-byte `int64` timestamp payload.
- `8` (`PONG`): Phone -> PC echoes `8`-byte payload.

### Flags Bitmask (`flags`)
- `0x01`: `KEYFRAME`
- `0x02`: `CODEC_CONFIG`
- `0x04`: `FRONT_CAMERA`
- `0x18` (bits 3-4, `(flags >> 3) & 0x03`): `ROTATION_CODE` (`0`=0°, `1`=90°, `2`=180°, `3`=270° clockwise to upright)
- `0x20`: `MIRRORED_BY_SENSOR`

## UDP Discovery JSON (Port 54821, every 1.5s)
```json
{
  "magic": "PCAM_DISC",
  "version": 1,
  "deviceName": "Pixel 8 Pro",
  "tcpPort": 54820,
  "pinRequired": true,
  "facing": "front",
  "streaming": true
}
```
