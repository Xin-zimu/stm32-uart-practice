# UART TX 环形缓冲与 TXE 中断学习笔记

本文专门说明当前 STM32F103 UART 工程新增的非阻塞发送机制，包括：

- 为什么要从轮询发送改为 TX 环形缓冲
- 环形缓冲中的 `head` 和 `tail`
- TXE 中断如何逐字节发送
- `printf` 和二进制协议如何共用发送通道
- 为什么协议必须整帧入队
- 临界区如何保护生产者和消费者
- 队列满时如何处理
- 当前实现的限制和后续扩展方向

相关文件：

- `SYSTEM/usart/uart_tx.c`
- `SYSTEM/usart/uart_tx.h`
- `SYSTEM/usart/usart.c`
- `User/app_protocol_practice.c`
- `练习/uart_tx_test_host.c`
- `Project/led.uvprojx`

## 1. 改造前的问题

改造前存在两套轮询发送代码。

`printf` 最终进入 `fputc()`，等待串口发送完成：

```c
while ((USART1->SR & 0X40) == 0)
{
    ...
}
```

二进制协议也会在发送每个字节后等待 TXE：

```c
while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET)
{
    ...
}
```

这种方式的特点是：

```text
CPU 写一个字节
CPU 等待硬件
CPU 再写下一个字节
CPU 再等待硬件
```

主要问题：

1. 主循环在发送期间不能处理其他任务。
2. 输出内容越长，主循环停顿时间越长。
3. 即使加入超时，也只是避免永久卡死，仍然属于阻塞等待。
4. `printf` 和协议层分别直接访问 USART，发送策略不统一。
5. 协议逐字节发送时，如果中途失败，线上可能出现残缺帧。

## 2. 改造后的总体结构

现在发送路径变为：

```text
主循环中的 printf
        |
        v
UartTx_TryByte()
        |
        +--------------------+
                             |
主循环中的二进制协议         |
        |                    |
        v                    v
UartTx_TryWrite() ----> TX 环形缓冲区
                             |
                             v
                     USART1 TXE 中断
                             |
                             v
                         USART1->DR
                             |
                             v
                         串口 TX 引脚
```

主循环只负责把数据复制到内存缓冲区，然后立即返回。

真正等待串口硬件发送的过程由 TXE 中断完成，主循环不再轮询硬件标志。

## 3. 为什么新建 uart_tx.c 和 uart_tx.h

发送队列属于 USART 驱动能力，不属于协议业务逻辑。

因此单独建立：

```text
SYSTEM/usart/uart_tx.c
SYSTEM/usart/uart_tx.h
```

职责划分：

| 文件 | 职责 |
| --- | --- |
| `usart.c` | GPIO、USART、NVIC 初始化，统一 USART1 IRQ，`printf` 重定向 |
| `uart_tx.c` | TX 环形缓冲、空间检查、整块入队、TXE 中断取数 |
| `app_protocol_practice.c` | 构造协议帧并调用发送队列，不直接操作 USART |

这样做的好处：

- 协议层不依赖具体 USART 寄存器。
- `printf` 和协议使用同一个发送通道。
- 环形缓冲可以独立测试。
- 后续切换到 DMA 时，协议层接口可以尽量保持不变。

## 4. 环形缓冲是什么

当前缓冲区定义：

```c
#define UART_TX_BUFFER_SIZE 256u

static uint8_t s_tx_buffer[UART_TX_BUFFER_SIZE];
static volatile uint16_t s_tx_head;
static volatile uint16_t s_tx_tail;
```

它是一块首尾相连的逻辑空间：

```text
索引：

0 1 2 3 ... 253 254 255
^                     |
|_____________________|
```

当索引到达 255 后，下一个位置回到 0。

### head

```text
s_tx_head
```

表示生产者下一次写入数据的位置。

当前生产者是主循环中的：

- `printf`
- 二进制协议回复

### tail

```text
s_tx_tail
```

表示消费者下一次读取并发送的位置。

当前消费者只有：

```text
USART1 TXE 中断
```

### 空队列

```c
s_tx_head == s_tx_tail
```

说明没有待发送数据。

## 5. 为什么 256 字节只能存 255 字节

环形缓冲需要区分两种状态：

```text
空
满
```

如果允许 256 个位置全部存满，那么：

```text
head == tail
```

既可能表示空，也可能表示满，无法区分。

当前实现故意保留一个空槽：

```c
#define UART_TX_BUFFER_CAPACITY (UART_TX_BUFFER_SIZE - 1u)
```

所以：

```text
数组大小：256 字节
实际容量：255 字节
```

判断规则：

```text
head == tail                 队列为空
next_head == tail            队列已满
```

这种实现简单且稳定。

## 6. 为什么使用 256 这种大小

256 是 2 的幂，可以使用位与完成索引回绕：

```c
#define UART_TX_BUFFER_MASK (UART_TX_BUFFER_SIZE - 1u)

next = (index + 1u) & UART_TX_BUFFER_MASK;
```

当 `index = 255`：

```text
(255 + 1) & 255
= 256 & 255
= 0
```

相比：

```c
next = (index + 1u) % 256u;
```

位与运算在单片机上通常更直接。

这也意味着：

```text
UART_TX_BUFFER_SIZE 必须保持为 2 的幂。
```

例如可以改为：

```text
64、128、256、512
```

不能随意改为 200，否则当前 MASK 算法不再正确。

## 7. 剩余空间如何计算

当前代码：

```c
static uint16_t UartTx_FreeFromSnapshot(uint16_t head, uint16_t tail)
{
    return (uint16_t)((tail - head - 1u) & UART_TX_BUFFER_MASK);
}
```

这里使用无符号运算和 MASK，同时处理普通情况与数组回绕情况。

### 初始空队列

```text
head = 0
tail = 0
```

计算：

```text
(0 - 0 - 1) & 255
= 0xFFFF & 255
= 255
```

所以空队列有 255 字节可用。

### 已使用 4 字节

```text
head = 4
tail = 0
```

计算：

```text
(0 - 4 - 1) & 255
= 251
```

剩余 251 字节，结果正确。

## 8. 整块入队流程

内部函数：

```c
static uint8_t UartTx_TryWriteInternal(
    const uint8_t *data,
    uint16_t len,
    uint16_t reserve
);
```

它执行以下步骤：

1. 检查 `len == 0`。
2. 检查 `data` 是否为空。
3. 检查长度是否超过队列最大容量。
4. 暂时关闭 USART1 TXE 中断。
5. 读取稳定的 `head` 和 `tail`。
6. 计算剩余空间。
7. 空间不足时整块拒绝。
8. 空间足够时复制全部数据。
9. 最后一次性更新 `s_tx_head`。
10. 开启 TXE 中断，启动发送。

核心保证：

```text
要么全部入队，要么一个字节也不入队。
```

这对二进制协议很重要。

## 9. 为什么只在最后更新 head

假设要入队 4 个字节：

```text
AA 55 01 02
```

不安全的写法是：

```text
写 AA
更新 head
写 55
更新 head
...
```

TXE 中断可能在中间看到更新后的 `head`，开始发送只写了一部分的数据。

当前做法：

```text
使用局部变量 head
写 AA
写 55
写 01
写 02
最后 s_tx_head = head
```

消费者只有在全部数据复制完成后才看见新的 `head`。

这相当于最后一步“发布”整块数据。

## 10. 为什么入队时只关闭 TXE 中断

生产者写缓冲区时，消费者可能同时在 TXE 中断中推进 `tail`。

当前代码使用：

```c
UART_TX_DISABLE_TXE_INTERRUPT();
```

只禁止 USART1 的 TXE 中断，不关闭：

- USART1 RXNE 接收中断
- TIM3 系统节拍中断
- 其他外设中断

优点：

- 临界区范围更小。
- 串口接收不会因为发送入队而被禁止。
- 系统节拍不会停止。

临界区内只进行：

- 读取索引
- 计算空间
- 最多复制一个短数据块
- 更新 `head`

不能在临界区内：

- 等待硬件
- 调用 `printf`
- 读取传感器
- 计算复杂业务
- 执行长循环

## 11. TXE 中断是什么

TXE 表示：

```text
Transmit Data Register Empty
发送数据寄存器为空
```

当 TXE 为 1 时，软件可以向：

```c
USART1->DR
```

写入下一个发送字节。

当前中断处理：

```c
void UartTx_IRQHandler(void)
{
    if (TXE 中断未开启 || TXE 未置位)
    {
        return;
    }

    if (队列为空)
    {
        关闭 TXE 中断;
        return;
    }

    USART1->DR = s_tx_buffer[s_tx_tail];
    s_tx_tail = next_tail;

    if (发送后队列为空)
    {
        关闭 TXE 中断;
    }
}
```

每次 TXE 中断只发送一个字节。

硬件开始处理该字节后，TXE 暂时清零；当数据寄存器再次可写时，
硬件重新置位 TXE，再次触发中断。

## 12. 为什么队列为空后必须关闭 TXE 中断

当发送数据寄存器为空时，TXE 会保持为 1。

如果队列已经为空但仍然开启 TXE 中断：

```text
进入中断
发现没有数据
退出中断
TXE 仍为 1
马上再次进入中断
```

CPU 会不断执行空中断，主循环几乎无法运行。

因此当前代码在以下两处关闭 TXE 中断：

1. 进入处理函数时发现队列已经为空。
2. 发送一个字节后发现该字节是队列最后一个字节。

关闭 TXE 中断不会取消已经写入 USART 硬件的数据。

## 13. TXE 和 TC 的区别

### TXE

```text
发送数据寄存器为空，可以写下一个字节。
```

适合连续发送数据。

### TC

```text
发送数据寄存器和移位寄存器都为空，
最后一个停止位也已经从 TX 引脚发出。
```

适合：

- RS-485 发送完成后关闭方向控制
- 关闭 USART 前确认发送彻底完成
- 精确确认物理线路已经发送完毕

当前工程只需要连续发送，因此使用 TXE。

如果以后加入 RS-485，需要在队列清空后继续等待 TC，再切换收发方向。

## 14. USART1 统一中断如何工作

STM32F103 的 USART1 接收和发送使用同一个中断入口：

```c
void USART1_IRQHandler(void)
```

当前入口依次处理：

```text
1. RXNE 接收中断
2. TXE 发送中断
```

简化流程：

```c
if (RXNE)
{
    读取一个接收字节;
    交给二进制协议或文本接收逻辑;
}

UartTx_IRQHandler();
```

`UartTx_IRQHandler()` 内部会自行判断：

- TXE 中断是否开启
- TXE 标志是否置位
- 队列是否有数据

因此即使本次 IRQ 只由 RXNE 触发，调用它也是安全的。

## 15. printf 如何使用环形缓冲

`printf` 最终调用：

```c
int fputc(int ch, FILE *f)
```

现在的实现：

```c
int fputc(int ch, FILE *f)
{
    uint8_t byte;

    (void)f;
    byte = (uint8_t)ch;
    (void)UartTx_TryByte(byte);
    return ch;
}
```

它不再等待 USART 硬件，只尝试入队一个字符。

如果调试队列空间不足：

```text
该字符被丢弃
主循环继续运行
drop_count 增加
```

这是有意的非阻塞策略。

调试文字可能不完整，但不能因为调试输出让业务程序卡死。

## 16. 为什么为协议保留 32 字节

当前最大二进制协议帧：

```text
帧头             2 字节
SEQ LEN CMD       3 字节
DATA             16 字节
CRC               2 字节
总计             23 字节
```

调试接口使用：

```c
UartTx_TryWriteDebug(...)
```

它要求入队完成后仍保留：

```c
#define UART_TX_PROTOCOL_RESERVE 32u
```

所以大量 `printf` 最多使用：

```text
255 - 32 = 223 字节
```

保留的 32 字节足够容纳当前最大 23 字节协议帧。

协议接口：

```c
UartTx_TryWrite(...)
```

可以使用全部剩余空间，不受 32 字节限制。

这是一种简单的优先级策略：

```text
协议响应优先
调试文字次要
```

## 17. 协议为什么必须先组完整帧

之前协议一边组帧一边调用单字节发送：

```text
发送 AA
发送 55
发送 SEQ
...
```

如果中途发送失败，线上会留下半帧。

现在先使用本地数组：

```c
uint8_t packet[PROTO_MAX_FRAME_LEN];
```

完整构造：

```text
AA 55 SEQ LEN CMD DATA CRC_LO CRC_HI
```

然后一次调用：

```c
UartTx_TryWrite(packet, packet_len);
```

结果只有两种：

```text
空间足够：完整帧全部进入队列
空间不足：完整帧一个字节也不进入队列
```

这样不会因为队列满而产生残缺协议帧。

## 18. 协议帧与调试文字是否会交叉

协议帧通过一次 `UartTx_TryWrite()` 整块入队。

在当前单生产者设计中，协议帧内部不会被 `printf` 字符插入。

发送顺序可能是：

```text
之前已经入队的调试文字
完整协议帧
之后入队的调试文字
```

协议帧自身保持连续。

但要注意：

```text
调试文字和二进制协议仍然共用同一根串口线。
```

如果上位机只按二进制协议解析，普通 ASCII 调试文字会被当作帧间垃圾，
解析器必须依靠 `AA 55` 帧头重新同步。

正式产品通常会考虑：

- 调试日志使用另一个串口
- 发布版本关闭调试日志
- 日志也封装成协议帧

## 19. drop_count 表示什么

接口：

```c
uint16_t UartTx_GetDropCount(void);
```

以下情况会增加发送丢弃计数：

- 数据指针为空但长度非 0
- 单次数据长度超过队列容量
- 调试数据会侵占协议保留空间
- 当前剩余空间不足以容纳整个数据块

当前 `drop_count` 同时统计：

```text
调试字符丢弃
协议整帧拒绝
非法发送参数
```

它适合判断发送压力是否过大，但不能直接区分是哪一类数据失败。

后续可以拆成：

```text
tx_debug_drop_count
tx_protocol_drop_count
tx_invalid_count
```

## 20. 当前实现的并发模型

当前设计假设：

```text
生产者：主循环
消费者：USART1 TXE 中断
```

主循环中的生产者包括：

- `printf`
- `App_ProtocolPractice_Task()`

消费者只有：

- `UartTx_IRQHandler()`

这是单生产者、单消费者模型。

重要限制：

```text
不要在其他中断服务程序中调用 UartTx_TryWrite()、
UartTx_TryWriteDebug() 或 printf。
```

原因：

- 当前入队保护只关闭 USART1 TXE 中断。
- 如果另一个更高优先级中断也进行入队，就会出现两个生产者。
- 两个生产者可能同时读取和修改 `s_tx_head`，导致数据覆盖。

如果以后确实需要中断中发送，应该重新设计：

- 使用全局临界区
- 使用专门的中断安全接口
- 或者让中断只投递事件，由主循环统一发送

推荐继续保持：

```text
中断只采集和投递
主循环统一输出
```

## 21. 为什么 head 和 tail 使用 volatile

```c
static volatile uint16_t s_tx_head;
static volatile uint16_t s_tx_tail;
```

原因：

- `head` 在主循环中修改，在中断中读取。
- `tail` 在中断中修改，在主循环中读取。

`volatile` 告诉编译器：

```text
这些变量可能在当前代码看不到的位置发生变化，
每次访问都必须真正读取或写入内存。
```

但 `volatile` 不等于线程安全。

真正的一致性仍然依赖：

- 单生产者、单消费者规则
- 入队时关闭 TXE 中断
- 最后一次性发布 `head`

## 22. 初始化顺序

`uart_init()` 中的顺序：

```text
配置 GPIO
配置 NVIC
配置 USART 参数
初始化 TX 环形缓冲
开启 RXNE 中断
使能 USART1
```

`UartTx_Init()` 会：

```c
关闭 TXE 中断;
head = 0;
tail = 0;
drop_count = 0;
```

初始化后队列为空，TXE 中断保持关闭。

第一次成功入队时：

```c
UART_TX_ENABLE_TXE_INTERRUPT();
```

如果 USART 数据寄存器为空，TXE 已经置位，CPU 会立即进入 USART1 IRQ，
发送队列中的第一个字节。

## 23. 主机测试验证了什么

测试文件：

```text
练习/uart_tx_test_host.c
```

它在 PC 上模拟：

- TXE 中断开关
- TXE 硬件标志
- USART 数据寄存器写入

覆盖场景：

1. 初始容量为 255 字节。
2. 入队后自动开启 TXE 中断。
3. 字节按原顺序发送。
4. 队列空后自动关闭 TXE 中断。
5. TXE 未置位时不会提前消耗队列。
6. 调试输出不能侵占协议保留空间。
7. 协议可以使用保留空间。
8. 空间不足时整块拒绝且队列不变。
9. `head` 和 `tail` 跨越数组末尾后仍能正常回绕。
10. 每个字节只发送一次，不丢失、不重复。

运行：

```powershell
gcc 练习\uart_tx_test_host.c -o 练习\output\uart_tx_test_host.exe
练习\output\uart_tx_test_host.exe
```

预期结尾：

```text
ALL UART TX TESTS PASSED
```

## 24. 常见错误

### 24.1 队列空后忘记关闭 TXE 中断

后果：

```text
CPU 持续进入空中断，主循环像卡死一样无法运行。
```

### 24.2 每写一个字节就立即更新 head

后果：

```text
消费者可能看到并发送尚未完成的数据块。
```

应先使用局部 `head` 完成复制，最后统一发布。

### 24.3 协议逐字节调用 TryByte

后果：

```text
队列在帧中间变满时，协议会被截断。
```

协议必须先组完整帧，再调用一次 `UartTx_TryWrite()`。

### 24.4 任意修改缓冲区大小

当前回绕使用 MASK，因此缓冲区大小必须为 2 的幂。

错误示例：

```c
#define UART_TX_BUFFER_SIZE 200u
```

### 24.5 在中断中调用 printf

当前实现不是多生产者安全的。

中断应设置标志或投递数据，主循环再执行打印。

### 24.6 把 TXE 当成物理发送完成

TXE 只表示数据寄存器可写，不代表最后一位已经离开 TX 引脚。

需要物理发送完成时应检查 TC。

## 25. 当前方案的优点

- 主循环不再轮询等待 USART 发送。
- `printf` 不会因为硬件发送状态永久卡死。
- 协议帧整块入队，不会因队列不足产生半帧。
- TXE 中断只执行一次取数、一次写 DR，处理时间短。
- RXNE 和 TXE 共用 USART1 IRQ。
- 调试文字自动为协议预留空间。
- 队列压力可以通过 `drop_count` 观察。
- 环形缓冲具有独立 PC 主机测试。

## 26. 当前方案的限制

- `printf` 队列满时会丢字符。
- 协议队列满时会整帧丢弃，目前调用者没有收到显式错误处理。
- `drop_count` 没有区分调试丢弃和协议丢弃。
- 只支持主循环单生产者。
- 没有提供“等待队列完全发送完”的 TC 接口。
- 尚未使用 DMA，发送每个字节仍会触发一次 TXE 中断。
- 本地没有 ARMCC/Keil 命令行编译器，仍需在 Keil 中实际 Build。

## 27. 后续可继续完善

### 返回协议入队结果

将：

```c
static void Protocol_SendFrame(...)
```

改为：

```c
static uint8_t Protocol_SendFrame(...)
```

让上层知道响应是否成功进入发送队列。

### 拆分丢弃统计

增加：

```text
调试字符丢弃数
协议帧拒绝数
非法参数次数
```

### 增加发送完成接口

例如：

```c
uint8_t UartTx_IsIdle(void);
```

判断：

```text
环形缓冲为空
并且 USART TC 已置位
```

### 升级为 DMA

当数据量更大时，可以保留类似的入队接口，把消费者从：

```text
每字节 TXE 中断
```

升级为：

```text
DMA 分块发送
```

## 28. 推荐阅读顺序

1. 阅读 `uart_tx.h`，理解公开接口。
2. 阅读 `s_tx_head`、`s_tx_tail` 和剩余空间公式。
3. 阅读 `UartTx_TryWriteInternal()` 的整块入队流程。
4. 阅读 `UartTx_IRQHandler()` 如何取出一个字节。
5. 阅读 `usart.c` 中 `fputc()` 的变化。
6. 阅读 USART1 IRQ 如何同时处理 RXNE 和 TXE。
7. 阅读协议层如何先构造 `packet[]` 再整帧入队。
8. 运行 `uart_tx_test_host.c` 并观察测试场景。

## 29. 最终记忆卡片

```text
缓冲区数组大小：256 字节
实际可用容量：255 字节
head：主循环下一次写入位置
tail：TXE 中断下一次发送位置
空：head == tail
满：next_head == tail
回绕：(index + 1) & 255
协议保留空间：32 字节
协议发送：完整组帧后整块入队
调试发送：空间不足允许丢字符
消费者：USART1 TXE 中断
队列为空：必须关闭 TXE 中断
当前并发模型：主循环单生产者 + TXE 中断单消费者
```
