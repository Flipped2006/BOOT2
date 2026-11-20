# BOOT2

面向 **STM32F407VE** 的两级串口 Bootloader。一级引导从 Flash 启动，二级引导在 RAM 中通过串口接收、写入和校验固件，并启动更新后的 APP。

![单包处理耗时对比，异步化前 49.53 ms，异步化后按时序推算约 32.76 ms](docs/benchmarks/packet-processing-time.png)

单包耗时按原始时序图比较。接收下一包时可并行写入上一包，约 **16.77 ms** 的 Flash 写入时间被重叠，原图标注单包耗时缩短约 **33.8%**。[异步化前时序](docs/项目展示图/ramboot异步化前时序图.png) · [异步化后时序](docs/项目展示图/ramboot异步化后时序图.png)

以 **2 Mbaud** 升级 **335.18 KiB** 的 `.xbin`，20 次实板升级的协议耗时均值为 **6.672 s**，P95 为 **6.761 s**，平均有效吞吐为 **50.24 KiB/s**。[逐次计时](docs/benchmarks/upgrade-protocol-samples.csv)

## 主要特性

- **两级引导与 RAM Boot。** 改造 ST 启动文件和链接脚本，由 Makefile 构建完整引导镜像。一级将二级镜像从 Flash 搬到 SRAM，二级在 SRAM 中运行升级逻辑，避开 Flash 忙时从同一片 Flash 取指带来的停顿。
- **定制协议与接收流水线。** 借鉴 Modbus RTU 的请求–应答方式，设计带命令、长度、载荷和 CRC16 的类 TLV 帧。RXNE 中断接收帧字段和普通载荷，DMA 搬运 `PROGRAM` 载荷。FreeRTOS 通过空闲、解析和烧写三条队列复用 10 个包缓冲，将命令解析与 Flash 写入交给独立任务。
- **Flash 分区防护与分层校验。** 擦除、烧写命令保护两级 Boot 所在的前 48 KiB。传输用 CRC16 校验单帧，写入后用 CRC32 校验 APP 和 Magic Header，启动前再次检查头部与镜像。
- **启动拦截与单帧重传。** 复位时长按升级键、启动窗口收到串口数据或镜像校验失败都会让设备留在 Boot。跳转时切换 `VTOR`，由汇编从 APP 向量表加载 `MSP` 和复位入口。TIM6 定期检查接收状态，帧内超时达到 60 ms 时中止当前接收并复位状态机，主机可重传这一帧。
- **配套打包、分发与实板测试。** AI 辅助开发 Python 打包脚本、C# WinForms 上位机和实板测试工具，覆盖编译 APP、封装 `.xbin`、串口分发和测试验证。

## 快速开始

需要 STM32F407VE 实板、SWD 下载器和支持 2 Mbaud 的 3.3 V USB 转串口。Bootloader 使用 Linux 或 WSL、GNU Make 和 Arm GNU Toolchain 构建。Windows 上位机使用 .NET 10 SDK。从源码构建示例 APP 需要 Keil MDK 与 Python 3。示例 APP 与打包脚本位于配套工程 `STM32F4_EASYLOGGER_BOOT`，上位机位于 `SERIAL_UPDATER`。

### 1. 构建并烧录 Bootloader

安装 `arm-none-eabi-gcc` 后，获取源码并构建。

```bash
git clone https://github.com/Flipped2006/BOOT2.git
cd BOOT2
make -j4
```

可用 `make -j4 GCC_PATH=/path/to/arm-gnu-toolchain/bin` 指定工具链目录。

通过 SWD 烧录 `build/boot1st.hex`。使用 `.bin` 烧录时，将 `build/boot1st.bin` 写入 **`0x08000000`**。这两个 Flash 镜像都已内嵌二级引导。调试 RAM 中的二级引导时可使用 `build/boot.elf` 的符号。[一级引导构建与验证](boot1st/README.md)

### 2. 连接升级串口与日志串口

USB 转串口的 TX 接板端 RX，RX 接板端 TX，并连接 GND。

| 用途 | 板端引脚 | 串口配置 |
| --- | --- | --- |
| 固件升级 | `USART3`，`PB10` TX / `PB11` RX | `2,000,000` baud，8N1 |
| 查看日志 | `USART1`，`PA9` TX / `PA10` RX | `115,200` baud，8N1 |

板上已有可启动的 APP 时，复位后按住 `KEY2`（`PC4`，低电平有效）约 3 秒，等日志出现 `enter boot mode` 后松开。

### 3. 打包并发送 APP

在 `STM32F4_EASYLOGGER_BOOT` 的 `mdk/stm32f407.uvprojx` 中构建示例 APP，链接起始地址为 `0x08010000`。Keil 的构建后命令会调用脚本生成 `.xbin`。已有 APP `.bin` 时，也可在配套 APP 工程根目录手动打包。

```powershell
python .\script\gen_magic_header.py .\mdk\Objects\stm32f407.bin
```

生成的 `generate/stm32f407_magic_header.xbin` 由 4 KiB 的 Magic Header 区域和 APP 二进制组成。已有兼容 `.xbin` 时可直接使用。在 `SERIAL_UPDATER` 工程根目录启动上位机。

```powershell
dotnet run --project SERIAL_UPDATER.csproj
```

选择升级串口和 `.xbin`，点击 **升级**。上位机依次写入并校验 APP 和 Magic Header，再发送 `BOOT`。在 USART1 上确认新 APP 的启动日志，示例 APP 会打印 `[SYSTEM]: Build Date:`。

## 启动与升级流程

```mermaid
flowchart LR
    stage1["一级引导 · Flash"] -->|复制二级镜像| stage2["二级引导 · SRAM"]
    host["PC 上位机 · .xbin"] -->|USART3 分帧传输| stage2
    stage2 -->|依次擦写和校验| flash["APP → Magic Header"]
    flash -->|启动校验通过| app["启动 APP"]
```

普通 `PROGRAM` 包入队后即可应答，让下一包接收与上一包写入重叠。每个写入阶段的最后一包等累计写入完成再应答，随后由 `VERIFY` 校验 Flash 内容。

| Flash 区域 | 起始地址或位置 |
| --- | --- |
| 一级引导 | `0x08000000` |
| 二级镜像 | 紧接一级引导，4 字节对齐，启动时搬至 `0x20000000` |
| Magic Header | `0x0800C000` |
| APP | `0x08010000` |

## 实板验证

2026-11-19 在 STM32F407VE 上使用 4 KiB 数据块测试。`.xbin` 由 339,124 字节 APP 与 4,096 字节头部组成。协议计时从开始 APP 更新到 Magic Header 校验完成，包含请求、擦除、分块写入和校验。[实板测试方法](tests/hil/README.md)

| 测试 | 通过 / 样本 | 通过条件 |
| --- | ---: | --- |
| 正常升级 | 20 / 20 | 两区校验通过，J-Link 读回一致，Boot 镜像保持完整，新 APP 启动并输出周期日志 |
| 普通帧截断后重传 | 10 / 10 | 超时后重传原帧，正确返回版本 |
| 固件帧截断后重传 | 10 / 10 | 超时后重传原帧，Flash 读回及 CRC32 校验通过 |
| CRC16 坏帧拦截 | 10 / 10 | 损坏帧被拒绝，正确的对照帧可写入 |
| CRC32 坏镜像拦截 | 10 / 10 | `VERIFY` 拒绝损坏镜像，设备留在 Boot |

## 相关文档

- [一级引导、向量表与链接检查](boot1st/README.md)
- [串口接收状态机与 DMA](docs/receive-state-machine.md)
- [完整数据流与源码位置](docs/project-data-flow.md#总数据流图)
- [实板测试配置与运行步骤](tests/hil/README.md)

## 许可证

BOOT2 自有代码采用 MIT 许可证。第三方组件的许可见各组件源码中的声明。[MIT 许可证](LICENSE)
