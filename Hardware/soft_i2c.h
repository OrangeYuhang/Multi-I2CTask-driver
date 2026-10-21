#ifndef _SOFT_I2C_H_
#define _SOFT_I2C_H_

#include <stdint.h>
#include "stm32f10x_gpio.h"
#define SCL GPIO_Pin_6
#define SDA GPIO_Pin_7
typedef enum {
    I2C_OK = 0,
    I2C_ERR_ACK,     
    I2C_ERR_TIMEOUT, 
    I2C_ERR_BUSY,    
} I2C_Status;

void SoftI2C_Init(void);
void SoftI2C_BusRecover(void);
uint8_t SoftI2C_WriteReg(uint8_t dev, uint8_t reg, uint8_t val);
uint8_t SoftI2C_ReadRegs(uint8_t dev, uint8_t reg, uint8_t *buf, uint8_t len);
uint8_t SoftI2C_SubmitReadRegs(uint8_t dev, uint8_t reg, uint8_t *buf, uint8_t len);
uint8_t SoftI2C_Busy(void);
uint8_t SoftI2C_Result(void);
void SoftI2C_SetSpeed(uint32_t freq_hz);
void SoftI2C_TickISR(void);

#endif
