#ifndef _GY521_H_
#define _GY521_H_
#include <stdint.h>
#include "stm32f10x_gpio.h"
#include "stm32f10x_exti.h"


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

/* GY521 的 INT 引脚接到 PA1，走 EXTI1 的上升沿。
 * 下面 5 个宏说的是同一个引脚，改接线时要一起改 ——
 * GPIO_Pin_1 / GPIO_PinSource1 / EXTI_Line1 是同一个引脚号的三种不同编码，
 * 对不上就会静默失效。 */
#define GY521_INT_PORT        GPIOA
#define GY521_INT_PORT_SOURCE GPIO_PortSourceGPIOA
#define GY521_INT_PIN         GPIO_Pin_1
#define GY521_INT_PIN_SOURCE  GPIO_PinSource1
#define GY521_INT_EXTI_LINE   EXTI_Line1

/* 量程换算系数：物理量 = 原始码值 / 系数。
 * 现在设的是陀螺 ±2000°/s、加速度 ±2g（见 GY521_Init 里的 GYRO_CONFIG /
 * ACCEL_CONFIG），改量程的话这两个数要跟着一起改，否则数值会差整数倍。 */
#define GY521_ACCEL_LSB_PER_G   16384.0f /* ±2g      : 16384 LSB/g      */
#define GY521_GYRO_LSB_PER_DPS  16.4f    /* ±2000°/s : 16.4  LSB/(°/s)  */

typedef enum {
    GY521_OK = 0,
    GY521_ERR_RESET,  /* 软复位命令没被应答，先查接线和上拉 */
    GY521_ERR_ID,     /* 复位成功但 WHO_AM_I 不是 0x68，多半是地址或器件不对 */
    GY521_ERR_CONFIG  /* 配置寄存器写失败 */
} GY521_Status;

typedef struct  {
  int16_t ax;
  int16_t ay;
  int16_t az;
  int16_t gx;
  int16_t gy;
  int16_t gz;

  /* MPU6050 说"有新数据了"。由 EXTI1 中断置 1，update 在任务开始时消费；
   * poll 只读它。
   * 必须是 volatile：中断里改、主循环里读，不然编译器可能把它
   * 优化进寄存器里，主循环永远看不到变化。 */
  volatile uint8_t data_ready;

  /* ax..gz 已经换成新的一组。由任务回调在解析完后置 1，
   * 显示逻辑处理完自己清零。
   *
   * 它和 data_ready 不是一回事：data_ready 是"芯片说数据好了"，
   * 这个是"数据已经落到 gy521 里了"。读取是异步的，两者之间隔着
   * 两笔 I2C 事务（先清中断锁存，再读 14 字节），所以显示必须看这个。 */
  volatile uint8_t sample_ready;

  /* 累计 I2C 失败次数。一直不涨才说明总线是健康的。 */
  uint16_t err_count;

} GY521;

/* 完整初始化：软复位 -> 校验 WHO_AM_I -> 唤醒选时钟源 -> 采样率/滤波/量程
 * -> 打开数据就绪中断 -> 配置 PA1/EXTI1 -> 注册任务。
 * 失败时返回具体是哪一步错了。 */
GY521_Status GY521_Init(void);

/* 任务节点、步进状态、事务缓冲都是 GY521.c 里的 file-scope static
 * （本文件只留数据），所以同一类设备只能有一个实例。 */

extern GY521 gy521;

#endif
