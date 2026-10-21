#include "stm32f10x.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_exti.h"
#include "GY521.h"
#include "i2c_task.h"
#include "soft_i2c.h"
#include "systick.h"

static I2CTask s_task;
static uint8_t gy521_poll(void);

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

    I2CTask_Register(&s_task);

    return GY521_OK;
}

enum { STEP_STATUS = 0, STEP_BURST, STEP_PARSE };

static uint8_t s_step;
static uint8_t s_int_status;
static uint8_t s_buf[GY521_BURST_LEN];

static uint8_t gy521_update(void)
{
    if (SoftI2C_Busy()) {
        return TASK_CONTINUE;
    }

    switch (s_step) {

    case STEP_STATUS:
        gy521.data_ready = 0;
        if (SoftI2C_SubmitReadRegs(GY521_ADDRESS, INT_STATUS, &s_int_status, 1) == 0U) {
            s_step = STEP_BURST;
        }
        return TASK_CONTINUE;

    case STEP_BURST:
        if (SoftI2C_Result() != I2C_OK) {
            gy521.err_count++;
            s_step = STEP_STATUS;
            return TASK_DONE;
        }

        if (SoftI2C_SubmitReadRegs(GY521_ADDRESS, ACCEL_X_H, s_buf, GY521_BURST_LEN) == 0U) {
            s_step = STEP_PARSE;
        }
        return TASK_CONTINUE;

    case STEP_PARSE:
        if (SoftI2C_Result() != I2C_OK) {
            gy521.err_count++;
            s_step = STEP_STATUS;
            return TASK_DONE;
        }

        gy521.ax = (int16_t)((s_buf[0] << 8) | s_buf[1]);
        gy521.ay = (int16_t)((s_buf[2] << 8) | s_buf[3]);
        gy521.az = (int16_t)((s_buf[4] << 8) | s_buf[5]);
        /* buf[6]、buf[7] 是温度，暂时用不到 */
        gy521.gx = (int16_t)((s_buf[8] << 8) | s_buf[9]);
        gy521.gy = (int16_t)((s_buf[10] << 8) | s_buf[11]);
        gy521.gz = (int16_t)((s_buf[12] << 8) | s_buf[13]);

        gy521.sample_ready = 1;
        s_step = STEP_STATUS;
        return TASK_DONE;

    default:
        s_step = STEP_STATUS;
        return TASK_DONE;
    }
}

static I2CTask s_task = {
    .update  = gy521_update,
    .poll    = gy521_poll,
    .freq_hz = 100000UL,
};

static uint8_t GY521_IntLevel(void)
{
    return (GY521_INT_PORT->IDR & GY521_INT_PIN) != 0U;
}

static uint8_t gy521_poll(void)
{
    return gy521.data_ready || GY521_IntLevel();
}

GY521 gy521 = {
    .data_ready = 0,
};
