# 接收状态机：len 被解析为 0x01AA 的原因

## 修改前复现

对当前修改前的 `bootloader.c` 使用按字节复制队列项的主机替身运行，得到：

```text
initial state=0 recv=0 (HEADER=0)
after partial AA 01: state=2 recv=0
after retry AA 01: len=0x01AA (426), state=3
fresh complete INQUERY: queued=1 len=1
recycled pointer after parser_task: (nil)
```

这里的 `state=2` 是 `PACKET_STATE_LEN`，`state=3` 是 `PACKET_STATE_PAYLOAD`。前一帧只收到 `AA 01` 时，状态机正在等两个长度字节；下一次请求的包头 `AA` 和 opcode `01` 被填入 `_temp[0..1]`，按小端序得到 `0x01AA`。

因此，426 并不是减法异常，而是两帧之间没有重新同步。426 本身也小于最大载荷，单纯检查长度上限无法发现这类错位。

当前代码在初次 `pvPortMalloc()` 后已有 `memset(packet, 0, sizeof(queue_packet_t))`，主机复现也确认初始状态为 HEADER、完整首包可以正常解析；不能将当前首包问题直接归因于“全局包没有初始化”。真正错误的复用路径是下面的代码：

```c
memset(&packet, 0, sizeof(packet));
xQueueSend(empty_queue, &packet, portMAX_DELAY);
```

`packet` 已经是指针。上述写法把指针变量清成 NULL，没有清理它指向的包，随后把 NULL 放回了空闲队列。`configASSERT(&packet)` 检查的是局部变量的地址，无法发现空指针。

## 丢字节来源与其他问题

接收回调中原来在收到包头、收到载荷和校验 CRC 时直接输出日志。升级串口是 2 Mbps、8N1，每字节约 5 微秒；日志串口为 115200 bps，每字符约 87 微秒，而且 `console_write()` 在中断中轮询发送。连续请求期间，这些日志会阻塞后续字节的读取。

STM32F407 的 USART 在 RXNE 尚未清除时收到后续字符，会出现 overrun 并丢失数据。参见 [ST RM0090，USART reception / Overrun error](https://www.st.com/resource/en/reference_manual/rm0090-stm32f407-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)。根据代码和速率判断，中断中的阻塞日志是本项目明确存在的丢字节风险；本次未采集实板 ORE 标志或串口波形，不能据主机复现断言每次丢字节都由它引起。

另外发现：

- PROGRAM 曾从 `payload - 1` 开始接收 `len + 1` 字节。`payload` 前面的一个字节正是 `len` 的高字节，会被 DMA 覆盖。
- USART3 在初始化时使能 DMA 接收请求，第一次 DMA 完成又关掉请求，下一次接收函数却没有重新使能；后续 PROGRAM 的状态可能停留在 PAYLOAD。
- 队列操作没有检查返回值，池耗尽时可能继续使用已经交给工作任务的包。

## 修改后的行为

`packet_reset()` 统一重置 `header/opcode/len/crc/_temp/_recv/_state`。首次分配、首次取包、从空闲队列取到新包、工作任务归还包，以及异常丢弃路径都回到 HEADER。工作任务使用 `memset(packet, 0, sizeof(*packet))` 清理实际对象，并用 `packet != NULL` 断言指针。

成功入解析队列后，接收端放弃旧包，尝试取一个新的空闲包；没有空闲包时将 `current_packet` 保持为 NULL 并丢弃输入，随后收到字节时重新尝试获取。入队失败时则保留接收端自己的缓冲区，清理状态等待重试。包在被队列或工作任务持有期间不会由接收端改写。

使用已有的 `BL_BYTE_RESET_TIME_MS=20` 作为字节间超时。下一个字节到达时，若旧帧未完成且已间隔至少 20 ms，先重置到 HEADER，再处理该字节；所以重试的 `AA` 会被当作新包头。该检查使用 FreeRTOS tick 的无符号差值，覆盖 tick 回绕。正在等待 PAYLOAD DMA 的阶段由 DMA 完成回调更新状态和时间；本次未增加未完成 DMA 的后台超时取消机制。

正确的 INQUERY 长度为 1 时，逐字节状态为：

| 收到的字段 | 接收后的状态 | `_recv` | `len` |
| --- | --- | ---: | ---: |
| `AA` | OPCODE | 0 | 0 |
| opcode `01` | LEN | 0 | 0 |
| 长度低字节 `01` | LEN | 1 | 0 |
| 长度高字节 `00` | PAYLOAD | 0 | 1 |
| 1 字节载荷 | CRC16 | 0 | 1 |
| CRC 低字节 | CRC16 | 1 | 1 |
| CRC 高字节 | 旧包交解析队列，新包回到 HEADER | 0 | 新包为 0 |

接收中断不再进行日志格式化或输出，完整包的日志在解析任务中输出。PROGRAM 的 DMA 地址和计数恢复为 `payload`、`len`，启动前停止流并清理标志，每次启动都使能 DMA 接收请求；完成后回到 RXNE 接收 CRC 的阶段。

## 验证

运行：

```sh
python3 tests/run_bootloader_regressions.py
```

交叉编译和 AddressSanitizer 主机测试通过，覆盖初始内存非零、取到带旧状态的空闲包、逐字节转换、连续三轮缓冲池复用、各阶段半包超时重试、超时前继续接收、tick 回绕、真正长度为 `0x01AA` 的合法包、CRC/长度错误、缓冲池耗尽与恢复，以及最大 PROGRAM 包的交接。

当前 ARM 函数栈帧为：`parser_task=40 B`、`program_task=16 B`、接收回调 `32 B`。仍保留原有的未使用 `key_trap_boot_check` 与链接段 RWX 警告。

新固件已生成于 `build/boot.elf`、`build/boot.hex`、`build/boot.bin`。未执行实板下载和持续高速接收验证；主机测试不模拟真实 UART overrun、DMA 时序和中断嵌套。现有 `.gitignore` 忽略 `tests/*`，本次保留了该设置，测试脚本作为本地验证文件使用。
