# Bootloader 五项实板测试

唯一入口：`bootloader_metrics.py`，依赖 Windows Python、`pyserial==3.5` 和 J-Link Commander。

| 测试函数 | 次数 | 通过条件 |
| --- | ---: | --- |
| `test_plain_retry` | 10 | 普通包截断后，超时原包重传一次，VERSION 正确 |
| `test_firmware_retry` | 10 | 固件包截断后重传一次，数据读回与 CRC32 正确 |
| `test_upgrade` | 20 | 两区 VERIFY、独立读回、Boot 不变、新 APP 启动后 3 次周期日志 |
| `test_crc16_reject` | 10 | 坏帧不改 Flash，随后有效对照帧能写入 |
| `test_crc32_reject` | 10 | 帧 CRC16 合法的坏镜像被 VERIFY/5 拒绝，且不能启动 |

先测试截断及坏数据，最后执行 20 次正常升级。任何设备错误立即停止，未执行项单列。
只支持当前 STM32F407VE、4096 字节数据块、4096 字节信息头 XBIN。运行前应已安装所选有效 APP；测试会擦写 APP/信息头。

测试会擦除并反复写入 APP 与信息头。先做截断和损坏测试，再做 20 次完整有效升级。若最后一次升级失败，额外尝试一次 2048 字节块的完整恢复，只作为清理，明确记录为不计分恢复；不会替换或抹去任何失败次数。恢复失败则保留失败状态和证据。

本地配置 `tests/hil/hil.local.json`示例，路径按实际修改：

```json
{
  "port": "COM5",
  "log_port": "COM6",
  "firmware": "E:/DevEnviroment/embedded/Boot/Boot/STM32F4_EASYLOGGER_BOOT/generate/stm32f407_magic_header.xbin",
  "jlink": "E:/DevTools/SEGGER/JLink/JLink.exe",
  "probe": "609021300"
}
```

```powershell
.\.venv-hil\Scripts\python.exe -m pip install pyserial==3.5
.\.venv-hil\Scripts\python.exe tests/hil/bootloader_metrics.py
```

升级口固定 2 Mbaud/8N1，日志口 115200；当前固件接收超时 60 ms，主机重传门限 150 ms。
APP 判据固定为新的 `[SYSTEM]: Build Date:` 之后至少 3 次 `[WiFi INFO]`，最长等待 40 秒，依赖当前 APP 的网络连接。
`results/<UTC时间>/` 保存逐次 CSV、汇总 JSON、串口记录、J-Link 记录及 Flash 读回；包含通过率、均值和 P95。设备错误或未执行项不算成功。

2026-11-19 的历史实测：上述五项分别 10/10、10/10、20/20、10/10、10/10；平均升级协议耗时 6.672 s，P95 6.761 s，吞吐 50.24 KiB/s。
历史原始证据保留在 `results/metrics/20261119T122226.558585Z/`，其中 `runner.py` 是当时实际执行的版本；当前精简版未重新运行实板。

CRC16/XMODEM：0x1021、初值 0；CRC32/IEEE：反射多项式 0xEDB88320。
实际配置为 COM5 升级、COM6 日志，J-Link SWD 普通复位/读回；
