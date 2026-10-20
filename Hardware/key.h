#ifndef _KEY_H_
#define _KEY_H_

#include "stm32f10x_gpio.h"
#include <stdint.h>

#define KEY_BOUNCE_TIME 20

/* 按键接的引脚。配置和读取都从这里取，避免出现"初始化 PB0、
 * 读的却是 PB1"这种对不上的情况 —— PB1 会一直停在复位后的浮空输入，
 * 读回来的是随机的环境噪声。换引脚只改这三行。 */
#define KEY_GPIO      GPIOB
#define KEY_GPIO_CLK  RCC_APB2Periph_GPIOB
#define KEY_GPIO_PIN  GPIO_Pin_0

typedef enum KeyState {
    KEY_STATE_IDLE,
    KEY_STATE_PRESSED,
    KEY_STATE_PRESS_DELAY,
    KEY_STATE_RELEASE_DELAY,

} KeyStateTypeDef;

typedef struct _Key {
    void (*init)(void);
    void (*update)(void);
    KeyStateTypeDef state;

    /* 有一次新的按下还没被处理。KeyUpdate 在消抖确认按下时置 1，
     * 使用者处理完自己清零。
     *
     * 为什么不直接用 state 判边沿：state 只在按住的这段时间里等于
     * KEY_STATE_PRESSED，如果主循环比按键动作还慢（比如循环里有
     * delay_ms(100)，而快按只持续 50ms），前后两次采样可能都落在
     * PRESSED 之外，这次按下就整个丢了。用标志位就不会丢事件。 */
    uint8_t pressed_event;
} Key;

void KeyInit(void);
void KeyUpdate(void);

extern Key key;
#endif
