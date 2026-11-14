# 启动 HardFault：定位与修复

## 结论和证据

直接原因是数据包按值复制造成任务栈溢出，队列中指针与结构体的混用还会破坏后续的数据交接。不是 `len -= chunk_size` 的算术错误，也没有发现链接器把全局变量放入栈区。

修改前的 `build/boot.elf` 与截图中的地址能够对应：

```text
0x08004090  PendSV_Handler
ldmia.w r0!, {r4-r11, lr}
```

这是 FreeRTOS 从下一个任务的保存栈中恢复寄存器的位置。截图中的故障不是发生在减法指令，而是发生在任务上下文恢复时。

截图寄存器含义：

- `HFSR = 0x40000000`：`FORCED=1`，其他故障升级为 HardFault。
- `CFSR = 0x00008200`：`PRECISERR=1`、`BFARVALID=1`，CPU 精确数据访问异常，故障地址有效。
- `BFAR = 0x00523920`：实际访问了非法地址。
- `SHCSR = 0`：BusFault 处理程序未使能，因此 BusFault 会升级为 HardFault。

Ozone 的 “a fault with configurable priority” 是对故障升级的说明，不能解读为“外设中断优先级配置错误”。本项目使用 `NVIC_PRIORITYGROUP_4`，调用 FreeRTOS ISR API 的 USART/DMA 中断优先级为 5，与 `configMAX_SYSCALL_INTERRUPT_PRIORITY = 5 << 4` 匹配。

寄存器定义和故障升级条件参见 [ST PM0214，2.4.2、4.4.12、4.4.14 节](https://www.st.com/resource/en/programming_manual/pm0214-stm32-cortexm4-mcus-and-mpus-programming-manual-stmicroelectronics.pdf)。

## 栈溢出的来源

`queue_packet_t` 含 4104 字节的 payload，ARM 目标上整个结构体为 4118 字节。旧代码在任务中定义局部结构体，再按值传给命令处理函数。编译器不仅为局部包分配空间，还要为按值参数准备空间。

使用项目原有 `-Og`、Cortex-M4F 参数和 GCC 15.3.1 实际编译得到的单函数栈帧如下。这些数字还没有计入调用链、异常入栈和 RTOS 上下文。

| 函数 | 修改前 | 修改后 |
| --- | ---: | ---: |
| `parser_task` | 8232 B | 40 B |
| `program_task` | 8232 B | 16 B |
| `bl_opcode_dispatch` | 4112 B | 8 B |
| `bl_usart_rx_handler` | 4168 B | 24 B |

两个工作任务的栈深度均为 2048 个 32 位字，即 8192 字节。旧代码仅入口栈帧就超出分配空间。接收回调的旧栈帧也超过链接脚本配置的 1024 字节 MSP 栈。

启动时主任务优先级为 4，随后创建的烧录和解析任务优先级分别为 2、1。主任务输出 `trap app failed` 日志时，`console_write()` 等待 DMA 完成信号量，让出了 CPU；工作任务便有机会执行其超大的入口栈帧，越过自己的栈边界，破坏相邻堆对象或任务上下文。因此越界的触发时机与日志等待、任务切换吻合。不能把调试器里 `len` 的异常值当成源头。

旧 ELF 中 `current_packet` 位于 `.bss`，地址 `0x200000e4`，大小 `0x1016`（4118）字节。FreeRTOS 任务栈由堆分配，栈越界可以破坏同一堆中的对象，这与“链接器把全局变量放在栈上”是不同的问题。

## 修改内容

- 包在堆上分配，`current_packet`、解析任务、烧录任务和全部命令处理函数都使用包指针。
- 三个队列统一传递 `queue_packet_t *`，入队传 `&packet`，出队接收到指针变量。
- 包依次归接收端、解析队列/任务、烧录队列/任务、空闲队列所有。非法命令也归还缓冲区；没有空闲包时丢弃输入，之后可重新获取缓冲区；入队失败时接收端保留自己的包。
- 接收状态保存在池中对象里；批量输入时推进数据指针。CRC 错误和长度超限的包不交给工作任务。
- 将接收中断中的日志移到解析任务，避免在有限的 MSP 栈中做日志格式化，也避免 RX 回调覆盖任务正在 DMA 输出的日志缓冲区。
- RX DMA 启动时使能 USART3 的 DMA 接收请求；PROGRAM 命令要求声明的烧录长度与实际载荷长度一致。
- 开启 FreeRTOS 栈溢出检查（模式 2）和内存分配失败钩子，检查任务创建结果，并使能 `uxTaskGetStackHighWaterMark()`。
- 编译生成 `build/*.su`，对大于 2048 字节的函数栈帧给出警告。

## 验证与上板复核

WSL 中运行：

```sh
cd /home/wang/BOOT2
python3 tests/run_bootloader_regressions.py
```

脚本构建固件并检查 ARM 栈帧，再以 AddressSanitizer 运行实际接收回调与两个工作任务。主机替身队列按 FreeRTOS 的行为复制队列项字节，覆盖初始化、指针一致性、批量接收、CRC/长度错误、入队失败、缓冲区耗尽与恢复、非法命令回收、最大 PROGRAM 包的 DMA 到烧录任务交接，以及烧录长度不匹配的拒绝路径。

构建产物为 `build/boot.elf`、`build/boot.hex`、`build/boot.bin`。构建仍有原有的未使用 `key_trap_boot_check` 和 RWX LOAD segment 警告；本次未改变按键启动策略或链接段布局。

本次完成的是交叉编译、目标栈用量检查和主机回归测试，未执行板上烧录与运行验证。Ozone 重新加载新 ELF 后，建议断点验证：

1. 进入 `parser_task`、`program_task`，确认均能阻塞在各自的 `xQueueReceive()`，随后返回主任务继续输出日志。
2. 运行 `uxTaskGetStackHighWaterMark()` 观察三个业务任务的历史剩余栈，返回值单位为 32 位字。不要只观察链接脚本中的 MSP 大小，它不是任务 PSP 栈的大小。
3. 在 `vApplicationStackOverflowHook`、`vApplicationMallocFailedHook`、`vAssertCalled` 设置断点。
4. 检查 `current_packet` 和各队列中的包指针位于 FreeRTOS 堆中，同一时刻一个包仅由一个接收端、队列或工作任务持有。

编译后的函数地址会变化，应使用新 ELF 的符号定位，不要继续沿用旧地址 `0x08004090` 设置断点。
