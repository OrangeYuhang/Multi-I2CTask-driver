#ifndef _GY521_H_
#define _GY521_H_
#include <stdint.h>
#include "stm32f10x_gpio.h"
#include "stm32f10x_exti.h"
#include "i2c_device.h"

/* GY521 模块 = MPU6050 + 3.3V 稳压 + 上拉电阻，直接读 MPU6050 的寄存器。
 *
 * 注意地址：0x68 是 datasheet 上的 7 位从机地址（AD0 接地时），
 * 真正发上总线的地址字节还要左移一位再补上读写方向位 ——
 * 这个移位在 soft_i2c 的引擎里做，驱动只提供 7 位地址。
 * 模块背面把 AD0 焊到 VCC 的话地址是 0x69。 */
#define GY521_ADDRESS 0x68

/* WHO_AM_I(0x75) 是芯片固化的身份寄存器，读出来必然是 0x68。
 * 用它判断"对面到底在不在、是不是 MPU6050"。 */
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
  I2CDevice base;

  int16_t ax;
  int16_t ay;
  int16_t az;
  int16_t gx;
  int16_t gy;
  int16_t gz;
  volatile uint8_t data_ready;
  volatile uint8_t sample_ready;
  uint8_t step;
  uint16_t err_count;
  uint8_t int_status;
  uint8_t buf[GY521_BURST_LEN];

} GY521;
GY521_Status GY521_Init(void);



extern GY521 gy521;

#endif
