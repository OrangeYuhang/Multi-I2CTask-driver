#ifndef _GY521_H_
#define _GY521_H_
#include <stdint.h>
#include "stm32f10x_gpio.h"
#include "stm32f10x_exti.h"
#define GY521_ADDRESS 0x68
#define WHO_AM_I 0x75
#define WHO_AM_I_VALUE 0x68

#define ACCEL_X_H 0x3B
#define ACCEL_X_L 0x3C
#define ACCEL_Y_H 0x3D
#define ACCEL_Y_L 0x3E
#define ACCEL_Z_H 0x3F
#define ACCEL_Z_L 0x40
#define TEMP_H    0x41
#define TEMP_L    0x42
#define GYRO_X_H 0x43
#define GYRO_X_L 0x44
#define GYRO_Y_H 0x45
#define GYRO_Y_L 0x46
#define GYRO_Z_H 0x47
#define GYRO_Z_L 0x48
#define ACCEL_CONFIG 0x1C
#define GYRO_CONFIG 0x1B
#define PWR_MGMT_1 0x6B
#define SMPLRT 0x19
#define CONFIG 0x1A
#define INT_PIN_CFG 0x37
#define INT_ENABLE 0x38
#define INT_STATUS 0x3A

/* 0x3B~0x48 共 14 个字节：加速度 6 + 温度 2 + 陀螺 6 */
#define GY521_BURST_LEN 14

#define GY521_INT_PORT        GPIOA
#define GY521_INT_PORT_SOURCE GPIO_PortSourceGPIOA
#define GY521_INT_PIN         GPIO_Pin_1
#define GY521_INT_PIN_SOURCE  GPIO_PinSource1
#define GY521_INT_EXTI_LINE   EXTI_Line1

#define GY521_ACCEL_LSB_PER_G   16384.0f /* ±2g      : 16384 LSB/g      */
#define GY521_GYRO_LSB_PER_DPS  16.4f    /* ±2000°/s : 16.4  LSB/(°/s)  */

typedef enum {
    GY521_OK = 0,
    GY521_ERR_RESET,  
    GY521_ERR_ID,     
    GY521_ERR_CONFIG  
} GY521_Status;

typedef struct  {
  int16_t ax;
  int16_t ay;
  int16_t az;
  int16_t gx;
  int16_t gy;
  int16_t gz;
  volatile uint8_t data_ready;
  volatile uint8_t sample_ready;
  uint16_t err_count;

} GY521;
GY521_Status GY521_Init(void);
extern GY521 gy521;

#endif
