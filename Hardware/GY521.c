#include "stm32f10x.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_exti.h"
#include "GY521.h"
#include "soft_i2c.h"
#include "systick.h"

static uint8_t GY521_WriteReg(uint8_t reg, uint8_t data)
{
    return SoftI2C_WriteReg(GY521_ADDRESS, reg, data);
}

static uint8_t GY521_ReadRegs(uint8_t reg, uint8_t *buf, uint8_t len)
{
    return SoftI2C_ReadRegs(GY521_ADDRESS, reg, buf, len);
}
static void GY521_IntPinInit(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = GY521_INT_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GY521_INT_PORT, &GPIO_InitStructure);
    GPIO_EXTILineConfig(GY521_INT_PORT_SOURCE, GY521_INT_PIN_SOURCE);

    EXTI_InitTypeDef EXTI_InitStructure;
    EXTI_InitStructure.EXTI_Line = GY521_INT_EXTI_LINE;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    EXTI_ClearITPendingBit(GY521_INT_EXTI_LINE);
    NVIC_SetPriority(EXTI1_IRQn, 1);
    NVIC_EnableIRQ(EXTI1_IRQn);
}

GY521_Status GY521_Init(void)
{
    SoftI2C_Init();
    delay_ms(100);
    SoftI2C_BusRecover();

    if (GY521_WriteReg(PWR_MGMT_1, 0x80)) {
        return GY521_ERR_RESET;
    }
    delay_ms(100);
    uint8_t id = 0;
    if (GY521_ReadRegs(WHO_AM_I, &id, 1) || id != WHO_AM_I_VALUE) {
        return GY521_ERR_ID;
    }
    if (GY521_WriteReg(PWR_MGMT_1, 0x01)) {
        return GY521_ERR_CONFIG;
    }
    delay_ms(10);

    if (GY521_WriteReg(SMPLRT, 0x09)) {
        return GY521_ERR_CONFIG;
    }

    if (GY521_WriteReg(CONFIG, 0x03)) {
        return GY521_ERR_CONFIG;
    }

    if (GY521_WriteReg(GYRO_CONFIG, 0x18)) {
        return GY521_ERR_CONFIG;
    }
    if (GY521_WriteReg(ACCEL_CONFIG, 0x00)) {
        return GY521_ERR_CONFIG;
    }
    if (GY521_WriteReg(INT_PIN_CFG, 0x20)) {
        return GY521_ERR_CONFIG;
    }
    if (GY521_WriteReg(INT_ENABLE, 0x01)) {
        return GY521_ERR_CONFIG;
    }

    GY521_IntPinInit();

    return GY521_OK;
}

enum { STEP_STATUS = 0, STEP_BURST, STEP_PARSE };
static uint8_t gy521_update(I2CDevice *self)
{
    GY521 *g = (GY521 *)self;
    if (SoftI2C_Busy()) {
        return TASK_CONTINUE;
    }

    switch (g->step) {

    case STEP_STATUS:
        if (SoftI2C_SubmitReadRegs(GY521_ADDRESS, INT_STATUS, &g->int_status, 1) == 0U) {
            g->step = STEP_BURST;
        }
        return TASK_CONTINUE;

    case STEP_BURST:
        if (SoftI2C_Result() != I2C_OK) {
            g->err_count++;
            g->step = STEP_STATUS;
            return TASK_DONE;
        }

        if (SoftI2C_SubmitReadRegs(GY521_ADDRESS, ACCEL_X_H, g->buf, GY521_BURST_LEN) == 0U) {
            g->step = STEP_PARSE;
        }
        return TASK_CONTINUE;

    case STEP_PARSE:
        if (SoftI2C_Result() != I2C_OK) {
            g->err_count++; 
            g->step = STEP_STATUS;
            return TASK_DONE;
        }

        g->ax = (int16_t)((g->buf[0] << 8) | g->buf[1]);
        g->ay = (int16_t)((g->buf[2] << 8) | g->buf[3]);
        g->az = (int16_t)((g->buf[4] << 8) | g->buf[5]);
        /* buf[6]、buf[7] 是温度，暂时用不到 */
        g->gx = (int16_t)((g->buf[8] << 8) | g->buf[9]);
        g->gy = (int16_t)((g->buf[10] << 8) | g->buf[11]);
        g->gz = (int16_t)((g->buf[12] << 8) | g->buf[13]);

        g->sample_ready = 1;
        g->step = STEP_STATUS;
        return TASK_DONE;

    default:
        g->step = STEP_STATUS;
        return TASK_DONE;
    }
}

/* INT 引脚的当前电平。MPU6050 的 INT 是推挽输出，PA1 配的是下拉输入，
 * 所以直接读 IDR 就是真实电平；模块没接时下拉保证读到 0，不会误触发。 */
static uint8_t GY521_IntLevel(void)
{
    return (GY521_INT_PORT->IDR & GY521_INT_PIN) != 0U;
}

/* 扫描标志 -> 入队。主循环每轮都调（见 GY521.h）。 */
void GY521_Scan(void)
{
    if (!gy521.data_ready && !GY521_IntLevel()) {
        return;
    }
    if (gy521.base.active) {
        return;
    }

    gy521.data_ready = 0;
    I2CDevice_Enqueue(&gy521.base);
}

/* 方法表：const 放 flash，所有 GY521 实例共享一张。 */
static const I2CDeviceVTable GY521_VTable = {
    .update = gy521_update,
};

GY521 gy521 = {
    .base = {
        .vptr    = &GY521_VTable,
        .freq_hz = 100000UL,
    },
    .data_ready = 0,
};
