# UART RX 环形缓冲

日期：2026-06-10

## 修改目标

为 USART1 增加独立 RX 环形缓冲，使接收中断只负责读取硬件字节并入队，
协议解析和文本命令处理统一转移到主循环。

## 修改前行为

- USART1 RXNE 中断直接执行二进制协议状态机、CRC 校验和文本行组包。
- 文本接收使用 `USART_RX_BUF` 与 `USART_RX_STA` 单行共享缓冲。
- 上一行尚未处理时，接收路径不能继续保存下一行完整数据。

## 修改后行为

- 新增 256 字节 RX 环形缓冲，保留一个空槽，实际容量为 255 字节。
- RXNE 中断读取 `USART1->DR` 后立即调用 `UartRx_PushFromIrq()`，不再解析协议或文本。
- 主循环通过 `UartRx_TryRead()` 按 FIFO 顺序取出字节，先交给二进制协议状态机，
  未被协议消费的字节再进入文本命令组行。
- RX 队列满时丢弃新字节并通过 `UartRx_GetDropCount()` 累计溢出次数。
- 超长文本行会整行丢弃到下一个 CR/LF，避免拆成多条错误命令。

## 逻辑变化范围

- 增加 RX 环形缓冲的初始化、入队、出队、可读数量和溢出计数接口。
- USART1 中断改为 RX 入队加原有 TXE 队列处理。
- 文本行缓冲状态从 USART 驱动移动到 `app_uart_practice`。
- 协议解析改为主循环上下文执行，并移除不再需要的 pending 邮箱全局关中断复制。
- 未修改协议帧格式、命令语义、TX 环形缓冲策略和串口参数。

## 涉及文件

- `SYSTEM/usart/uart_rx.c`
- `SYSTEM/usart/uart_rx.h`
- `SYSTEM/usart/usart.c`
- `SYSTEM/usart/usart.h`
- `User/app_uart_practice.c`
- `User/app_uart_practice.h`
- `User/app_protocol_practice.c`
- `User/app_protocol_practice.h`
- `Project/led.uvprojx`
- `练习/uart_rx_test_host.c`
- `docs/change-logs/2026-06-10-uart-rx-ring-buffer.md`

## 接口与兼容性

- 新增 `UartRx_Init()`、`UartRx_PushFromIrq()`、`UartRx_TryRead()`、
  `UartRx_GetAvailable()` 和 `UartRx_GetDropCount()`。
- 删除仅供旧单行接收方案使用的 `USART_RX_BUF` 与 `USART_RX_STA` 外部变量。
- `uart_init()`、文本命令和二进制协议的外部调用方式保持不变。
- USART1 仍使用 115200、8N1，TX 与 RX 继续共用同一个 IRQ 入口。

## 编码与注释规范

- C/H：GB2312 兼容代码页 936，无 BOM。
- 文档及脚本：UTF-8，无 BOM。
- 函数前详细注释，函数内仅保留关键注释。
- 宏、枚举和结构字段使用对齐的行尾 // 注释。

## 验证结果

- RX 主机测试：通过，覆盖空队列、255 字节容量、满队列拒绝、溢出计数、
  FIFO 顺序和跨数组尾部回绕，结尾为 `ALL UART RX TESTS PASSED`。
- TX 回归主机测试：通过，结尾为 `ALL UART TX TESTS PASSED`。
- 新增函数注释检查：通过。
- STM32 文本编码策略检查：通过。
- `git diff --check`：通过。
- Keil 隔离构建：`0 error(s), 0 warning(s)`。
- Keil 尺寸：`Code=12188, RO-data=736, RW-data=152, ZI-data=1776`。

## Git

- 分支：codex/crc16-upgrade
- 起始提交：2f0b912082a8c02b1c50093b93733ee8d6d25e68
- Commit：this commit
- 提交说明：Add interrupt-driven UART RX ring buffer
