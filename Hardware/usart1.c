#include "usart1.h"
#include "systick.h"

/* ------------------------------- 私有状态 ---------------------------------- */

/* 发送环形缓冲：rear 只被 SendFrame 写、front 只被 Update 写（都在主循环
 * 上下文），没有并发问题。留 1 字节空隙区分"满"和"空"：front == rear
 * 永远表示空，所以实际能装 SIZE-1 个字节。 */
static uint8_t usart_tx_buffer[USART1_TX_BUFFER_SIZE];
static uint16_t usart_tx_front = 0;
static uint16_t usart_tx_rear = 0;
static USART1_TxState usart_tx_state = USART1_TX_IDLE;

/* 接收环形缓冲：rear 只被中断写、front 只被 Update 写 —— 单生产者/单消费
 * 者模式。任何一方读对方的指针时最多拿到"落后一步"的旧值，旧值只会让
 * 判断偏保守（少算 1 字节空间），不会越界，所以无需关中断/临界区。
 * 前提是 uint16_t 的读写是单条指令（Cortex-M3 上对齐的 16 位访问天然原子）。 */
static volatile uint8_t usart_rx_buffer[USART1_RX_BUFFER_SIZE];
static volatile uint16_t usart_rx_front = 0;
static volatile uint16_t usart_rx_rear = 0;

/* 帧解析状态机（只被主循环上下文访问） */
static USART1_RxState usart_rx_state = USART1_RX_WAIT_HEAD1;
static uint8_t usart_rx_frame[USART1_FRAME_DATA_MAX];
static uint8_t usart_rx_len = 0;           /* 本帧数据区总长度 */
static uint8_t usart_rx_index = 0;         /* 已收到的数据字节数 */
static uint8_t usart_rx_sum = 0;           /* SUM 累加器（uint8_t 自然回绕） */
static uint8_t usart_rx_frame_ready = 0;   /* 1 = 完整帧就绪，等上层取走 */
static uint32_t usart_rx_last_byte_ms = 0; /* 帧超时判定用 */

/* 错误计数拆成两份：byte 错误由中断累加、frame 错误由主循环累加，
 * 避免两个上下文同时 ++ 同一个变量（读-改-写竞争会丢计数）。 */
static volatile uint16_t usart_rx_byte_errors = 0; /* 奇偶/帧错误、缓冲溢出 */
static uint16_t usart_rx_frame_errors = 0;         /* SUM 错、非法长度、超时 */

/* ------------------------------- 寄存器级初始化 ---------------------------- */

void USART1_Init(void)
{
    /* 1. 开时钟：GPIOA 和 USART1 都挂在 APB2（USART1 是高速外设） */
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_USART1EN;

    /* 2. GPIO（引脚 8~15 在 CRH）：
     *    PA9  = TX：复用推挽输出 50MHz   MODE=11, CNF=10 -> 0xB
     *    PA10 = RX：浮空输入             MODE=00, CNF=01 -> 0x4 */
    GPIOA->CRH &= ~(0xFu << 4 | 0xFu << 8);
    GPIOA->CRH |= 0xBu << 4 | 0x4u << 8;

    /* 3. 波特率：BRR = PCLK / 波特率（寄存器是 4 位小数的定点数）。
     *    72MHz / 115200 = 625 = 0x271，恰好整除，零误差 */
    USART1->BRR = SystemCoreClock / USART1_BAUDRATE;

    /* 4. 帧格式：CR2 全 0 = 1 停止位；CR1：M=0（8 数据位，复位值）+ 偶校验
     *    PCE=1、PS=0。只开 RXNE 接收中断：发送走主循环轮询（发送不会丢
     *    数据只是变慢，不值得为它开中断） */
    USART1->CR2 = 0;
    /* 8N1：不用硬件奇偶校验。CH341 的 Linux 驱动对校验位支持有缺陷，
     * 带校验位的流会被收成结构化乱码；完整性交给帧级的 SUM。 */
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;

    /* 5. 最后开总开关（RM0008 建议 UE 最后置位）。UE 一开 TXE 立即为 1，
     *    Update() 随时可以把缓冲里的数据泵出去 */
    USART1->CR1 |= USART_CR1_UE;

    /* 6. 中断优先级 1：能抢断主循环里的任何代码（SysTick 是最低的 15）。
     *    默认优先级分组 0（4 位全给抢占），1 合法且高于 SysTick */
    NVIC_SetPriority(USART1_IRQn, 1);
    NVIC_EnableIRQ(USART1_IRQn);
}

/* ------------------------------- 中断服务 ---------------------------------- */

/* USART1_IRQHandler 的实体（ISR 壳在 User/stm32f10x_it.c）。
 * 只开了一个中断源 RXNE，进来只干三件事：读 SR、读 DR、入环形缓冲。 */
void USART1_IRQ_Service(void)
{
    /* 先读 SR 再读 DR：这是硬件规定的清标志顺序 */
    uint32_t sr = USART1->SR;

    if (sr & USART_SR_RXNE) {
        uint8_t byte = (uint8_t)USART1->DR;

        if (sr & (USART_SR_PE | USART_SR_FE)) {
            /* 字节级错误：这个字节是垃圾，直接丢弃。帧里缺一个字节
             * -> SUM 对不上 -> 整帧作废，状态机自己会重同步 */
            usart_rx_byte_errors++;
        } else {
            uint16_t next = (usart_rx_rear + 1) & (USART1_RX_BUFFER_SIZE - 1);
            if (next != usart_rx_front) {
                usart_rx_buffer[usart_rx_rear] = byte;
                usart_rx_rear = next;
            } else {
                /* 环形缓冲满：主循环没及时消费（帧没取走/循环卡太久）。
                 * 丢新字节而不是覆盖旧字节，保证已收数据的完整性 */
                usart_rx_byte_errors++;
            }
        }
    }

    if (sr & USART_SR_ORE) {
        /* 上一个字节没读走、新的又来了，硬件把新字节丢了。若上面刚
         * 读过 DR，本标志已被顺带清除；这里只负责计数。丢失的字节
         * 由帧级 SUM + 重同步兜底，不会卡死 */
        usart_rx_byte_errors++;
    }
}

/* ------------------------------- 帧解析状态机 ------------------------------ */

/* 一次喂一个字节。帧格式见 usart1.h 顶部注释 */
static void USART1_ParseByte(uint8_t byte)
{
    usart_rx_last_byte_ms = millis();

    switch (usart_rx_state) {

    case USART1_RX_WAIT_HEAD1:
        if (byte == USART1_FRAME_HEADER1) {
            usart_rx_state = USART1_RX_WAIT_HEAD2;
        }
        break;

    case USART1_RX_WAIT_HEAD2:
        if (byte == USART1_FRAME_HEADER2) {
            usart_rx_sum = USART1_FRAME_HEADER1 + USART1_FRAME_HEADER2;
            usart_rx_state = USART1_RX_WAIT_LEN;
        } else if (byte != USART1_FRAME_HEADER1) {
            /* 既不是 0x55 也不是连续帧头 -> 重新找帧头 */
            usart_rx_state = USART1_RX_WAIT_HEAD1;
        }
        /* byte == 0xAA：留在本状态，把它当新的第一个帧头（0xAA 0xAA 0x55
         * 这种数据也能正确同步） */
        break;

    case USART1_RX_WAIT_LEN:
        if (byte == 0 || byte > USART1_FRAME_DATA_MAX) {
            /* 非法长度，整帧作废。LEN=0 被拒：ReceiveFrame 用返回值 0
             * 表示"没有帧"，空数据帧会让上层分不清"没帧"和"空帧" */
            usart_rx_state = USART1_RX_WAIT_HEAD1;
            usart_rx_frame_errors++;
        } else {
            usart_rx_len = byte;
            usart_rx_index = 0;
            usart_rx_sum += byte;
            usart_rx_state = USART1_RX_RECV_DATA;
        }
        break;

    case USART1_RX_RECV_DATA:
        usart_rx_frame[usart_rx_index++] = byte;
        usart_rx_sum += byte;
        if (usart_rx_index == usart_rx_len) {
            usart_rx_state = USART1_RX_WAIT_SUM;
        }
        break;

    case USART1_RX_WAIT_SUM:
        if (byte == usart_rx_sum) {
            usart_rx_frame_ready = 1;  /* 完整帧就绪，暂停解析等上层取走 */
        } else {
            usart_rx_frame_errors++;
        }
        usart_rx_state = USART1_RX_WAIT_HEAD1;
        break;
    }
}

/* ------------------------------- 状态机推进 -------------------------------- */

void USART1_Update(void)
{
    /* ---------- 发送状态机：每次最多推进 1 字节，绝不阻塞 ---------- */
    if (usart_tx_state == USART1_TX_SENDING) {
        if (usart_tx_front != usart_tx_rear) {
            /* 缓冲还有数据：TXE=1（DR 空）就把下一个字节填进去 */
            if (USART1->SR & USART_SR_TXE) {
                USART1->DR = usart_tx_buffer[usart_tx_front];
                usart_tx_front = (usart_tx_front + 1) & (USART1_TX_BUFFER_SIZE - 1);
            }
        } else if (USART1->SR & USART_SR_TC) {
            /* 缓冲空 + TC=1（移位寄存器也空，最后一个停止位已发出）才是
             * 真发完。TXE 只说明 DR 空，不能当"发完"用 */
            usart_tx_state = USART1_TX_DONE;
        }
    }

    /* ---------- 接收：从环形缓冲取字节喂给帧解析状态机 ---------- */
    if (!usart_rx_frame_ready) {
        /* 半截帧超时：进入解析后 FRAME_TIMEOUT_MS 内没完成就放弃。
         * WAIT_HEAD1 不计时——那本来就是"等下一帧"状态 */
        if (usart_rx_state != USART1_RX_WAIT_HEAD1 &&
            millis() - usart_rx_last_byte_ms > USART1_FRAME_TIMEOUT_MS) {
            usart_rx_state = USART1_RX_WAIT_HEAD1;
            usart_rx_frame_errors++;
        }

        while (usart_rx_front != usart_rx_rear) {
            uint8_t byte = usart_rx_buffer[usart_rx_front];
            usart_rx_front = (usart_rx_front + 1) & (USART1_RX_BUFFER_SIZE - 1);
            USART1_ParseByte(byte);
            if (usart_rx_frame_ready) {
                break;  /* 帧就绪：停下等上层取走，剩余字节留在缓冲里 */
            }
        }
    }
}

/* -------------------------------- 发送 ------------------------------------- */

USART1_State USART1_SendFrame(const uint8_t *data, uint16_t len)
{
    uint8_t frame[USART1_FRAME_DATA_MAX + USART1_FRAME_OVERHEAD];
    uint16_t frame_len;
    uint16_t free_space;
    uint16_t i;

    if (len == 0 || len > USART1_FRAME_DATA_MAX) {
        return USART1_TX_LEN_INVALID;
    }

    /* 1. 组帧：帧头 + 长度 + 数据 + 和校验（uint8_t 自然回绕 = & 0xFF） */
    frame[0] = USART1_FRAME_HEADER1;
    frame[1] = USART1_FRAME_HEADER2;
    frame[2] = (uint8_t)len;
    frame[3 + len] = USART1_FRAME_HEADER1 + USART1_FRAME_HEADER2 + (uint8_t)len;
    for (i = 0; i < len; i++) {
        frame[3 + i] = data[i];
        frame[3 + len] += data[i];
    }
    frame_len = len + USART1_FRAME_OVERHEAD;

    /* 2. 原子性：要么整帧入队、要么一个字节都不进（空间不足直接拒绝，
     *    调用方稍后重试。入半帧会把帧协议彻底打乱） */
    free_space = (USART1_TX_BUFFER_SIZE - 1) -
                 ((usart_tx_rear - usart_tx_front) & (USART1_TX_BUFFER_SIZE - 1));
    if (frame_len > free_space) {
        return USART1_TX_BUF_FULL;
    }

    /* 3. 入队 + 唤醒发送状态机（IDLE/DONE -> SENDING，Update 会开始泵数据） */
    for (i = 0; i < frame_len; i++) {
        usart_tx_buffer[usart_tx_rear] = frame[i];
        usart_tx_rear = (usart_tx_rear + 1) & (USART1_TX_BUFFER_SIZE - 1);
    }
    if (usart_tx_state != USART1_TX_SENDING) {
        usart_tx_state = USART1_TX_SENDING;
    }
    return USART1_TX_OK;
}

/* -------------------------------- 接收 ------------------------------------- */

uint16_t USART1_ReceiveFrame(uint8_t *data, uint16_t max_len)
{
    uint16_t i;

    if (!usart_rx_frame_ready) {
        return 0;
    }

    /* 数据区超出 max_len 的部分截断丢弃（帧整体仍被消费掉） */
    if (max_len > usart_rx_len) {
        max_len = usart_rx_len;
    }
    for (i = 0; i < max_len; i++) {
        data[i] = usart_rx_frame[i];
    }

    usart_rx_frame_ready = 0;  /* 取走，状态机恢复解析 */
    return max_len;
}

/* ------------------------------- 状态查询 ---------------------------------- */

USART1_TxState USART1_GetTxState(void)
{
    USART1_TxState state = usart_tx_state;

    if (state == USART1_TX_DONE) {
        usart_tx_state = USART1_TX_IDLE;  /* 边沿事件：读一次自动清掉 */
    }
    return state;
}

USART1_RxState USART1_GetRxState(void)
{
    return usart_rx_state;
}

uint16_t USART1_GetRxErrorCount(void)
{
    return usart_rx_byte_errors + usart_rx_frame_errors;
}
