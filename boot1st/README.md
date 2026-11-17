# 一级引导

在 WSL 的 `/home/wang/BOOT2` 下执行：

```sh
make -j4
# 也可以只请求生成完整引导镜像，依赖的二级引导会自动构建：
make boot1st -j4
```

默认使用项目内的 `tools/gcc-arm-none-eabi/15.3/bin` 工具链；也可以通过
`make GCC_PATH=/path/to/toolchain/bin -j4` 指定其他工具链目录。

烧录 `build/boot1st.hex`，或将 `build/boot1st.bin` 烧录到 **0x08000000**。
`build/boot1st.elf` 也包含完整的 Flash 烧录内容，并带有一级引导的调试符号。
`build/boot.elf`、`boot.bin`、`boot.hex` 是链接到 RAM 的二级引导产物；其中
`boot.bin` 由 `.incbin` 自动嵌入一级引导，单独烧录它到 Flash 不能从复位启动。

## 地址布局

| 内容 | 地址 |
| --- | --- |
| 一级引导向量表 | `0x08000000` |
| 一级引导代码 | 紧接向量表，运行于 Flash |
| 二级引导固件镜像 | 一级引导代码后按 4 字节对齐，符号 `__boot2_flash_start__` |
| 二级引导运行地址及向量表 | `0x20000000` |
| SRAM 顶部及初始 MSP | `0x20020000` |
| 现有 magic header | `0x0800C000` |
| 现有应用程序 | `0x08010000` |

`boot1st.ld` 将整个引导镜像限制在已有的 48KB Boot 保护区内。超出该区域，
或二级镜像不包含完整向量表时，链接会失败。二级镜像的源地址、RAM 目的地址和
长度均来自链接器符号，无需手工填写固件大小。修改 RAM 起始地址时，需同时
更新 `boot1st.ld` 与 `platforms/cmsis/device/stm32f407xx_ram.ld`。

## 复位流程

1. MCU 从 Flash 向量表装载 MSP 和一级 `Reset_Handler`。
2. 一级引导屏蔽可屏蔽中断，用寄存器完成 Flash 到 RAM 的复制，包含不足 4 字节的尾部。
3. 检查二级向量表：MSP 必须在 SRAM 内且按 8 字节对齐；复位入口必须是位于已复制镜像内的 Thumb 地址。
4. 设置 `SCB->VTOR = 0x20000000`，执行 `DSB/ISB`，装载二级 MSP，恢复中断后跳转。
5. 二级引导原有的 `Reset_Handler` 执行 `SystemInit`、`.data` 初始化、`.bss` 清零和 C 运行时初始化，再进入 `main`。

二级链接脚本中 `.data` 的加载地址与运行地址相同，一级已复制初始化数据，
二级启动文件的这次复制因此是原址复制；`.bss`、堆和栈不占用 Flash 镜像。
一级引导以硬件/系统复位后的寄存器及外设状态为前提，不负责清理一个正在运行的 RTOS。
向量检查失败或一级发生异常时停在 Flash 中的 `Default_Handler`，便于设置断点。

调试 RAM 中的二级程序时使用 `build/boot.elf` 的符号；一级 ELF 中的
`.boot2_image` 只是一份嵌入的二进制，不包含二级程序的函数符号。

## 验证

```sh
python3 boot1st/test_boot1st.py
# Python 环境已安装 unicorn 时，可额外执行实际 ARM 指令：
python3 boot1st/test_boot1st.py --emulate
```

如果尚未安装 Unicorn，可以在隔离的 Python 环境中运行：

```sh
python3 -m venv /tmp/boot1st-verify
/tmp/boot1st-verify/bin/python -m pip install unicorn
/tmp/boot1st-verify/bin/python boot1st/test_boot1st.py --emulate
```

检查最终镜像、两级向量、初始化数据以及超长/不完整镜像的链接拒绝；指令仿真
还检查逐字复制、1/2/3 字节尾部、复制边界、VTOR/MSP/中断恢复和非法向量停机。
并继续执行二级引导原有的启动代码，检查 `SystemInit` 后向量仍位于 RAM、
`.data` 初值保持完整、`.bss` 清零且 C 运行时初始化后到达 `main`。
仿真在 `main` 入口结束，板上外设与 FreeRTOS 的运行仍需上板验证。
