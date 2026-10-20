#include "stm32f10x.h"
#include "stm32f10x_gpio.h"
#include "systick.h"
#include "key.h"

void KeyInit(void){
    RCC_APB2PeriphClockCmd(KEY_GPIO_CLK, ENABLE);
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = KEY_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(KEY_GPIO, &GPIO_InitStructure);
    key.state = KEY_STATE_IDLE;
    key.pressed_event = 0;
}

void KeyUpdate(void){
    static uint32_t key_time = 0;
    uint8_t key_level = GPIO_ReadInputDataBit(KEY_GPIO, KEY_GPIO_PIN);
    switch (key.state) {
        case KEY_STATE_IDLE:
            if (key_level == 0) {
                key_time = millis();
                key.state = KEY_STATE_PRESS_DELAY;
            }
            break;

        case KEY_STATE_PRESS_DELAY:
            if (millis() - key_time > KEY_BOUNCE_TIME) {
                if(key_level == 0) {
                    key.state = KEY_STATE_PRESSED;
                    /* 消抖确认了一次按下，发出事件。一次按下只会走到这里一次 */
                    key.pressed_event = 1;
                } else {
                    key.state = KEY_STATE_IDLE;
                }
            }
            break;

        case KEY_STATE_PRESSED:
            if (key_level == 1){
                key_time = millis();
                key.state = KEY_STATE_RELEASE_DELAY;
            }
            break;


        case KEY_STATE_RELEASE_DELAY:
            if(millis() - key_time > KEY_BOUNCE_TIME){
                if(key_level == 1) {
                    key.state = KEY_STATE_IDLE;
                } else {
                    /* 松手时的抖动，还是同一次按下，不算新事件 */
                    key.state = KEY_STATE_PRESSED;
                }
            }
            break;
    }
}

Key key = {
    .init = KeyInit,
    .update = KeyUpdate,
    .state = KEY_STATE_IDLE,
    .pressed_event = 0
};