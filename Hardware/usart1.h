#ifndef _USART1_H_
#define _USART1_H_

#include "stm32f10x.h"

/* ===========================================================================
 * USART1 驱动：115200 / 8 数据位 / 无校验 / 1 停止位（串口助手选 8N1）
 * （不用偶校验：CH341 的 Linux 驱动对校验位支持有缺陷，见 usart1.c）
 * 引脚：PA9 = TX（复用推挽 50MHz），PA10 = RX（浮空输入），未重映射
 * 全部直接操作寄存器（位宏来自 Start/stm32f10x.h），不用任何 SPL 库函数。
 *
 * 架构：接收中断 + 环形缓冲，发送轮询推进
 *   - RXNE 中断把 DR 里的字节收进 RX 环形缓冲。主循环就算被别处阻塞
 *     ~11ms（128 字节 x 86.8us，比如 OLED 的 I2C 忙等）也不会丢字节；
 *   - USART1_Update() 在主循环周期调用：发送状态机（TX 轮询，每次最多
 *     推进 1 字节）+ 帧解析状态机（消费 RX 环形缓冲）。全程非阻塞。
 *
 * 帧协议（收/发同格式，共 LEN+4 字节）：
 *   | 0xAA | 0x55 | LEN | DATA[0..LEN-1] | SUM |
 *   帧头1   帧头2  数据长度(1~64) 数据区     和校验
 *   SUM = (0xAA + 0x55 + LEN + ΣDATA) & 0xFF（低 8 位，溢出丢弃）
 *
 * 完整性：帧级由 SUM 保证（不匹配整帧作废）；失步靠帧头自动重同步，
 * 半截帧靠超时自动放弃。
 * 注意 LEN 不允许为 0：ReceiveFrame 用返回值 0 表示"没有帧"。
 * =========================================================================== */

#define USART1_BAUDRATE         115200u /* 72MHz 下 BRR=625，零误差 */
#define USART1_FRAME_HEADER1    0xAAu   /* 帧头第 1 字节 */
#define USART1_FRAME_HEADER2    0x55u   /* 帧头第 2 字节 */
#define USART1_FRAME_DATA_MAX   64u     /* 数据区最大字节数 */
#define USART1_FRAME_OVERHEAD   4u      /* 帧头 2 + 长度 1 + 校验 1 */
#define USART1_FRAME_TIMEOUT_MS 50u     /* 半截帧超时，自动回到等帧头 */
#define USART1_RX_BUFFER_SIZE   128u    /* 接收环形缓冲（2 的幂） */
#define USART1_TX_BUFFER_SIZE   128u    /* 发送环形缓冲（2 的幂） */

/* 发送状态机状态 */
typedef enum {
    USART1_TX_IDLE,      /* 空闲：无数据在发 */
    USART1_TX_SENDING,   /* 发送中：缓冲或移位寄存器里还有数据 */
    USART1_TX_DONE,      /* 上一批数据已全部发出（含停止位） */
} USART1_TxState;

/* 接收解析状态机状态（USART1_GetRxState 查询，调试时打到屏幕上） */
typedef enum {
    USART1_RX_WAIT_HEAD1, /* 等帧头 0xAA */
    USART1_RX_WAIT_HEAD2, /* 等帧头 0x55 */
    USART1_RX_WAIT_LEN,   /* 等数据长度 */
    USART1_RX_RECV_DATA,  /* 收数据区 */
    USART1_RX_WAIT_SUM,   /* 等校验和 */
} USART1_RxState;

/* SendFrame 的返回值 */
typedef enum {
    USART1_TX_OK,          /* 组帧完成，已入队 */
    USART1_TX_BUF_FULL,    /* 发送缓冲空间不足，整帧未入队，稍后重试 */
    USART1_TX_LEN_INVALID, /* len 为 0 或超过 USART1_FRAME_DATA_MAX */
} USART1_State;

void USART1_Init(void);

/* 主循环里周期调用：推进发送状态机 + 帧解析状态机。非阻塞，单次几 us。 */
void USART1_Update(void);

/* 自动组帧（帧头/长度/SUM）后放入发送缓冲，非阻塞 */
USART1_State USART1_SendFrame(const uint8_t *data, uint16_t len);

/* 取出一个完整帧的数据区，返回数据区长度；0 = 当前没有完整帧。
 * 完整帧就绪后请及时取走：取走之前后续帧不会被解析（字节留在环形缓冲
 * 里，主循环长时间不取会把缓冲顶满、开始丢字节）。 */
uint16_t USART1_ReceiveFrame(uint8_t *data, uint16_t max_len);

/* 发送状态机状态。读到 USART1_TX_DONE 会自动清回 IDLE（边沿事件语义）。 */
USART1_TxState USART1_GetTxState(void);

/* 接收解析状态机当前位置 */
USART1_RxState USART1_GetRxState(void);

/* 累计错误计数（只增不减，观察它是否持续增长可判断链路健康）：
 * 字节级奇偶/帧错误 + 环形缓冲溢出 + SUM 错误 + 非法长度 + 帧超时 */
uint16_t USART1_GetRxErrorCount(void);

/* 供 User/stm32f10x_it.c 的 USART1_IRQHandler 调用（本工程约定 ISR 壳在
 * it.c、业务逻辑在模块里，同 SysTick_Handler -> SysTick_IncTick） */
void USART1_IRQ_Service(void);

#endif /* _USART1_H_ */
