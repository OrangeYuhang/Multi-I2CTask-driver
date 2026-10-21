/**
  ******************************************************************************
  * @file    User/main.c
  * @brief   软件模拟 I2C 读 GY521(MPU6050) 六轴原始数据，显示在 OLED 上。
  *
  * 接线：
  *     GY521   SCL -> PA6    SDA -> PA7    INT -> PA1
  *             VCC -> 3.3V   GND -> GND
  *     OLED    SCL -> PB6    SDA -> PB7    VCC -> 3.3V   GND -> GND
  *     按键    PB0 -> GND
  * 两路 I2C 用的是不同引脚：GY521 走软件模拟(PA6/PA7)，
  * OLED 走硬件 I2C1(PB6/PB7)，互不干扰。
  *
  * 数据是中断驱动的：MPU6050 每准备好一组新数据就把 INT 拉高，
  * EXTI1 中断只置标志位。I2C 的收发由 TIM2 中断里的软 I2C 引擎驱动；
  * 主循环扫到标志就把采样任务入队，再让任务队列 runtime 推进它 ——
  * 读数据不阻塞主循环，刷屏和采样是真正并行跑的。
  *
  * 屏幕显示的是原始码值，不是物理量。换算关系见 GY521.h：
  *     加速度(g)   = ax / 16384.0
  *     角速度(°/s) = gx / 16.4
  ******************************************************************************
  */

#include "GY521.h"
#include "i2c_task.h"
#include "oled.h"
#include "systick.h"
#include "key.h"
#include "usart1.h"
#include <stdio.h>
#include <string.h>
#define DRAW_INTERVAL_MS 100

int main(void)
{
    SysTick_Init();
    OLED_Init();
    key.init();

    USART1_Init(); 

    if (GY521_Init() != GY521_OK) {
        oled.show_string(1, 1, "GY521 FAIL");
        while (1) {
        }
    }

    oled.show_string(1, 1, "GY521");
    oled.show_string(1, 7, "e:"); 
    oled.show_string(1, 12, "ACCEL");

    uint8_t page = 0;
    uint32_t last_draw = millis() - DRAW_INTERVAL_MS;

    while (1) {
        key.update();
        USART1_Update();

        I2CTask_Poll();
        I2CTask_Run();
        if (key.pressed_event) {
            key.pressed_event = 0;
            page = !page;
            oled.show_string(1, 12, page ? "GYRO " : "ACCEL");
            last_draw = millis() - DRAW_INTERVAL_MS;
        }

        if (millis() - last_draw < DRAW_INTERVAL_MS) {
            continue;
        }
        last_draw = millis();

        oled.show_num(1, 9, gy521.err_count, 2);

        if (!gy521.sample_ready) {
            continue;
        }
        gy521.sample_ready = 0;

        {
            char line[64];
            sprintf(line, "ax=%6d ay=%6d az=%6d gx=%6d gy=%6d gz=%6d",
                    gy521.ax, gy521.ay, gy521.az,
                    gy521.gx, gy521.gy, gy521.gz);
            USART1_SendFrame((const uint8_t *)line, strlen(line));
        }

        if (page) {
            oled.show_string(2, 1, "gx");
            oled.show_signed_num(2, 4, gy521.gx, 6);
            oled.show_string(3, 1, "gy");
            oled.show_signed_num(3, 4, gy521.gy, 6);
            oled.show_string(4, 1, "gz");
            oled.show_signed_num(4, 4, gy521.gz, 6);
        } else {
            oled.show_string(2, 1, "ax");
            oled.show_signed_num(2, 4, gy521.ax, 6);
            oled.show_string(3, 1, "ay");
            oled.show_signed_num(3, 4, gy521.ay, 6);
            oled.show_string(4, 1, "az");
            oled.show_signed_num(4, 4, gy521.az, 6);
        }
    }
}
