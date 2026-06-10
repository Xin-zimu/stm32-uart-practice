# UART RX 分发与完整消息队列

日期：2026-06-10

## 修改目标

将 UART 接收路径进一步拆成中断搬运、主循环解析和业务执行三层，并为
二进制协议帧与文本命令行增加独立的完整消息队列。

## 修改前行为

- USART1 RXNE 中断已经只负责把字节写入 RX 环形缓冲。
- `App_UARTPractice_Task()` 同时负责读取 RX 环形缓冲、协议字节分发、
  文本组行，并在协议帧完成时直接调用协议业务任务。
- 二进制协议只使用一个 `pending` 帧槽，连续完整帧可能因槽位忙而丢弃。
- 文本行完成后立即执行业务，没有完整文本行队列。
- README 和 TX 学习文档仍保留部分旧的中断内解析说明。

## 修改后行为

- 新增 `App_UartRxDispatch_Task()`，专门从 RX 环形缓冲读取字节，并按
  “二进制协议优先、未消费字节进入文本解析器”的顺序进行分发。
- 分发任务不执行命令；每次最多处理 32 字节，并在一条协议帧尝试或
  文本行结束后让出 CPU。
- 协议解析完成后写入 4 槽完整帧 FIFO，协议业务任务每次执行一帧。
- 文本组行完成后写入 2 槽完整行 FIFO，文本业务任务每次执行一行。
- 队列满时保留旧消息、丢弃新消息，并提供独立的丢弃计数接口。
- README 和 TX 环形缓冲学习文档已更新为新的三层接收结构。

## 逻辑变化范围

- 新增独立 RX 字节分发任务并加入主循环和 Keil 工程。
- 将协议单帧邮箱替换为 4 槽 FIFO，增加队列长度和帧丢弃计数接口。
- 将文本行完成即执行改为 2 槽 FIFO，增加逐字节输入、队列长度和
  文本行丢弃计数接口。
- 协议输入接口增加“文本字节、继续接收、帧结束”三种返回状态。
- 保持 USART1 中断、RX/TX 环形缓冲、协议帧格式、命令字和业务语义不变。

## 涉及文件

- `User/app_uart_rx_dispatch.c`
- `User/app_uart_rx_dispatch.h`
- `User/app_protocol_practice.c`
- `User/app_protocol_practice.h`
- `User/app_uart_practice.c`
- `User/app_uart_practice.h`
- `User/main.c`
- `Project/led.uvprojx`
- `练习/uart_rx_pipeline_test_host.c`
- `README.md`
- `uart_tx_ring_buffer_learning.md`
- `docs/change-logs/2026-06-10-uart-rx-dispatch-queues.md`

## 接口与兼容性

- 新增 `App_UartRxDispatch_Task()`。
- 新增协议输入结果宏、`App_ProtocolPractice_GetQueueCount()` 和
  `App_ProtocolPractice_GetFrameDropCount()`。
- 新增 `App_UARTPractice_FeedByte()`、
  `App_UARTPractice_GetLineQueueCount()` 和
  `App_UARTPractice_GetLineDropCount()`。
- 原有初始化函数、业务任务函数、文本命令和二进制协议格式保持兼容。
- USART1 仍为 115200、8N1，RXNE 中断仍只读取 DR 并写 RX 环形缓冲。

## 编码与注释规范

- C/H：GB2312 兼容代码页 936，无 BOM。
- 文档及脚本：UTF-8，无 BOM。
- 函数前详细注释，函数内仅保留关键注释。
- 宏、枚举和结构字段使用对齐的行尾 // 注释。

## 验证结果

- RX 分层集成测试：通过，结尾为
  `ALL UART RX PIPELINE TESTS PASSED`。
- 集成测试覆盖 5 个连续协议帧、4 槽协议 FIFO、2 槽文本 FIFO、
  队列满丢弃计数和文本/二进制混合字节流。
- RX 环形缓冲回归测试：通过，结尾为 `ALL UART RX TESTS PASSED`。
- TX 环形缓冲回归测试：通过，结尾为 `ALL UART TX TESTS PASSED`。
- 新增函数注释检查：通过。
- STM32 文本编码策略检查：通过。
- `git diff --check`：通过。
- Keil 隔离构建：`0 error(s), 0 warning(s)`。
- Keil 尺寸：`Code=12472, RO-data=736, RW-data=160, ZI-data=2240`。

## Git

- 分支：codex/uart-rx-dispatch-queues
- 起始提交：49effedf8d89abf02ad353d323c6aac84d1526ad
- Commit：this commit
- 提交说明：Separate UART RX parsing from command execution
