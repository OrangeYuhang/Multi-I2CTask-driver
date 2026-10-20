#include "soft_i2c.h"
#include "stm32f10x.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_tim.h"
#include "systick.h"

/* ===========================================================================
 * 位定时
 *
 * SCL 100kHz → 位周期 10µs。TIM2 按 200kHz 跑，一个 tick = 半个位周期
 * (5µs)，每个 tick 推进一步：一个 tick 摆 SDA 抬高 SCL，下一个 tick
 * 采样/驱动再拉低 SCL。
 *
 * 注意 TIM2 的时钟不是 PCLK1(36MHz) 而是 72MHz —— STM32F1 的规则是
 * APB 预分频不为 1 时定时器时钟 = PCLKx × 2。本工程 PPRE1 = DIV2，
 * 所以 TIM2CLK = 36 × 2 = 72MHz。按 36MHz 算的话 SCL 会慢一倍。
 * =========================================================================== */
#define I2C_TIM_CLK_HZ    72000000UL
#define I2C_TICK_HZ       200000UL
#define I2C_TIM_ARR       (I2C_TIM_CLK_HZ / I2C_TICK_HZ - 1UL) /* 359 */

/* 一笔事务的 tick 上限。14 字节连读约 189 个位 → 378 tick ≈ 1.9ms，
 * 给 10ms 的余量，主要是容忍从机把 SCL 拉住不放（时钟拉伸）。 */
#define I2C_TIMEOUT_TICKS 2000U

#define I2C_PORT GPIOA

#define SDA_RELEASE() (I2C_PORT->BSRR = SDA)
#define SDA_LOW()     (I2C_PORT->BRR  = SDA)
#define SCL_RELEASE() (I2C_PORT->BSRR = SCL)
#define SCL_LOW()     (I2C_PORT->BRR  = SCL)
#define SDA_READ()    ((I2C_PORT->IDR & SDA) != 0U)
#define SCL_READ()    ((I2C_PORT->IDR & SCL) != 0U)

/* ---------------------------------------------------------------------------
 * 事务脚本
 *
 * 一笔事务展开成一串 op，引擎按顺序走。用脚本而不是给每种事务写一套
 * 状态机，是因为"写寄存器"和"读寄存器"只是 op 序列不同，位级逻辑完全一样。
 * --------------------------------------------------------------------------- */
enum { OP_START = 0, OP_TX, OP_RX, OP_STOP };

typedef struct {
    uint8_t op;
    uint8_t arg; 
} I2C_Op;

/* 最长的一笔是 ReadRegs(len)：START, TX(addr+w), TX(reg), START,
 * TX(addr+r), RX(len), STOP = 7 项 */
#define I2C_SCRIPT_MAX 7

enum { ST_IDLE = 0, ST_START, ST_TX, ST_RX, ST_STOP };

enum { PHASE_LOW = 0, PHASE_HIGH }; 

static I2C_Op        script[I2C_SCRIPT_MAX];
static uint8_t       script_len;
static uint8_t       script_pos;

static volatile uint8_t  stage;
static volatile uint8_t  phase;
static volatile uint8_t  bit;      /* 0~8，一个字节槽 = 8 数据位 + 1 应答位 */
static volatile uint8_t  tx_byte;
static volatile uint8_t  rx_byte;
static volatile uint8_t  sub;      /* START/STOP 的三拍子计数 */
static volatile uint16_t ticks;
static volatile uint8_t  result;

static uint8_t * volatile rx_buf;  /* 收数据的落点，走一个字节前进一格 */
static volatile uint8_t  rx_left;  /* 本次 OP_RX 还要收几个字节 */


static void engine_finish(uint8_t res)
{
    if (res != I2C_OK) {
        SDA_RELEASE();
        SCL_RELEASE();
    }
    result = res;
    stage  = ST_IDLE;

    TIM2->CR1 &= (uint16_t)~TIM_CR1_CEN;
}

static void load_op(void)
{
    if (script_pos >= script_len) {
        engine_finish(I2C_OK);
        return;
    }

    const I2C_Op *op = &script[script_pos];

    switch (op->op) {

    case OP_START:
        stage = ST_START;
        sub   = 0U;
        break;

    case OP_TX:
        stage   = ST_TX;
        tx_byte = op->arg;
        bit     = 0U;
        phase   = PHASE_LOW;
        break;

    case OP_RX:
        stage   = ST_RX;
        rx_left = op->arg;
        bit     = 0U;
        phase   = PHASE_LOW;
        break;

    case OP_STOP:
        stage = ST_STOP;
        sub   = 0U;
        break;

    default:
        engine_finish(I2C_OK);
        break;
    }
}

static void byte_done(void)
{
    bit   = 0U;
    phase = PHASE_LOW;

    if (stage == ST_TX) {
        script_pos++;
        load_op();
        return;
    }

    *rx_buf++ = rx_byte;
    rx_left--;

    if (rx_left > 0U) {
        return; 
    }
    script_pos++;
    load_op();
}

static uint8_t engine_submit(void)
{
    if (stage != ST_IDLE) {
        return 1U;
    }

    script_pos = 0U;
    ticks      = 0U;
    result     = I2C_OK;
    sub        = 0U;
    bit        = 0U;
    phase      = PHASE_LOW;

    load_op();
    if (stage == ST_IDLE) {
        return 0U;
    }

    TIM2->CNT = 0U;
    TIM2->SR  = (uint16_t)~TIM_SR_UIF;
    TIM2->CR1 |= (uint16_t)TIM_CR1_CEN;
    return 0U;
}

void SoftI2C_TickISR(void)
{
    TIM2->SR = (uint16_t)~TIM_SR_UIF; 

    if (++ticks > I2C_TIMEOUT_TICKS) {
        engine_finish(I2C_ERR_TIMEOUT);
        return;
    }

    switch (stage) {

    case ST_START:
        if (sub == 0U) {
            SDA_RELEASE();
            SCL_RELEASE();
        } else if (sub == 1U) {
            SDA_LOW();
        } else {
            SCL_LOW();
            script_pos++;
            load_op();
        }
        sub++;
        break;

    case ST_TX:
        if (phase == PHASE_LOW) {
            if (bit < 8U) {
                if ((tx_byte & (uint8_t)(0x80U >> bit)) != 0U) {
                    SDA_RELEASE();
                } else {
                    SDA_LOW();
                }
            } else {
                SDA_RELEASE();
            }
            SCL_RELEASE();
            phase = PHASE_HIGH;
        } else {
            if (!SCL_READ()) {
                break; /* 从机在拉伸时钟，下一拍再看 */
            }
            if (bit == 8U && SDA_READ()) {
                engine_finish(I2C_ERR_ACK); /* 第 9 位还是高 = 从机没应答 */
                return;
            }
            SCL_LOW();
            if (++bit < 9U) {
                phase = PHASE_LOW;
            } else {
                byte_done();
            }
        }
        break;

    case ST_RX:
        if (phase == PHASE_LOW) {
            if (bit == 8U) {
                if (rx_left > 1U) {
                    SDA_LOW();
                } else {
                    SDA_RELEASE();
                }
            } else {
                SDA_RELEASE();
            }
            SCL_RELEASE();
            phase = PHASE_HIGH;
        } else {
            if (!SCL_READ()) {
                break;
            }
            if (bit < 8U) {
                rx_byte = (uint8_t)((rx_byte << 1) | (SDA_READ() ? 1U : 0U));
            }
            SCL_LOW();
            if (++bit < 9U) {
                phase = PHASE_LOW;
            } else {
                byte_done();
            }
        }
        break;

    case ST_STOP:
        if (sub == 0U) {
            SDA_LOW();
        } else if (sub == 1U) {
            SCL_RELEASE();
        } else {
            SDA_RELEASE();
            engine_finish(I2C_OK);
            return;
        }
        sub++;
        break;

    default:
        break;
    }
}

/* ---------------------------------------------------------------------------
 * 脚本构造
 * --------------------------------------------------------------------------- */

static void build_write_script(uint8_t dev, uint8_t reg, uint8_t val)
{
    script[0] = (I2C_Op){ OP_START, 0U };
    script[1] = (I2C_Op){ OP_TX, (uint8_t)(dev << 1) };        /* 方向位 0 = 写 */
    script[2] = (I2C_Op){ OP_TX, reg };
    script[3] = (I2C_Op){ OP_TX, val };
    script[4] = (I2C_Op){ OP_STOP, 0U };
    script_len = 5U;
}

static void build_read_script(uint8_t dev, uint8_t reg, uint8_t *buf, uint8_t len)
{
    script[0] = (I2C_Op){ OP_START, 0U };
    script[1] = (I2C_Op){ OP_TX, (uint8_t)(dev << 1) };              /* 写，发寄存器指针 */
    script[2] = (I2C_Op){ OP_TX, reg };
    script[3] = (I2C_Op){ OP_START, 0U };                            /* 重复起始，中间不插 STOP */
    script[4] = (I2C_Op){ OP_TX, (uint8_t)((dev << 1) | 1U) };       /* 方向位 1 = 读 */
    script[5] = (I2C_Op){ OP_RX, len };
    script[6] = (I2C_Op){ OP_STOP, 0U };
    script_len = 7U;

    rx_buf = buf; 
}

/* ---------------------------------------------------------------------------
 * 对外接口
 * --------------------------------------------------------------------------- */

void SoftI2C_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    GPIO_SetBits(I2C_PORT, SCL | SDA);

    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin   = SCL | SDA;
    gpio.GPIO_Mode  = GPIO_Mode_Out_OD;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(I2C_PORT, &gpio);

    TIM_TimeBaseInitTypeDef tim;
    TIM_TimeBaseStructInit(&tim);
    tim.TIM_Prescaler     = 0U;
    tim.TIM_Period        = (uint16_t)I2C_TIM_ARR;
    tim.TIM_ClockDivision = TIM_CKD_DIV1;
    tim.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &tim);

    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
    NVIC_SetPriority(TIM2_IRQn, 0);
    NVIC_EnableIRQ(TIM2_IRQn);
    stage  = ST_IDLE;
    result = I2C_OK;
}

void SoftI2C_SetSpeed(uint32_t freq_hz)
{
    if (freq_hz != 50000UL && freq_hz != 100000UL) {
        return;
    }

    TIM2->ARR = (uint16_t)(I2C_TIM_CLK_HZ / (freq_hz * 2UL) - 1UL);
}

uint8_t SoftI2C_WriteReg(uint8_t dev, uint8_t reg, uint8_t val)
{
    if (stage != ST_IDLE) {
        return I2C_ERR_BUSY;
    }
    build_write_script(dev, reg, val);
    engine_submit();

    while (SoftI2C_Busy()) {
    }
    return SoftI2C_Result();
}

uint8_t SoftI2C_ReadRegs(uint8_t dev, uint8_t reg, uint8_t *buf, uint8_t len)
{
    if (stage != ST_IDLE) {
        return I2C_ERR_BUSY;
    }
    build_read_script(dev, reg, buf, len);
    engine_submit();

    while (SoftI2C_Busy()) {
    }
    return SoftI2C_Result();
}

uint8_t SoftI2C_SubmitReadRegs(uint8_t dev, uint8_t reg, uint8_t *buf, uint8_t len)
{
    if (stage != ST_IDLE) {
        return I2C_ERR_BUSY;
    }
    build_read_script(dev, reg, buf, len);
    return engine_submit();
}

uint8_t SoftI2C_Busy(void)
{
    return (stage != ST_IDLE) ? 1U : 0U;
}

uint8_t SoftI2C_Result(void)
{
    return result;
}

void SoftI2C_BusRecover(void)
{
    TIM2->CR1 &= (uint16_t)~TIM_CR1_CEN;
    stage  = ST_IDLE;
    result = I2C_OK;

    SDA_RELEASE();
    delay_us(5);

    for (uint8_t i = 0; i < 9U; i++) {
        SCL_LOW();
        delay_us(5);
        SCL_RELEASE();
        delay_us(5);
        if (SDA_READ()) {
            break;
        }
    }
    SDA_LOW();
    delay_us(5);
    SCL_RELEASE();
    delay_us(5);
    SDA_RELEASE();
    delay_us(5);
}
