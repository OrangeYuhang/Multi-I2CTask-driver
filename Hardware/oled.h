#ifndef _OLED_H_
#define _OLED_H_

#include <stdint.h>

/* ===========================================================================
 * 4 脚 I2C OLED (SSD1306 / SSD1315, 128x64) 调试打印模块
 *
 * 接线（模块正面丝印一般就是这 4 个脚）：
 *     GND -> GND
 *     VCC -> 3.3V
 *     SCL -> PB6   (I2C1_SCL)
 *     SDA -> PB7   (I2C1_SDA)
 *
 * 用的是 STM32F103C8T6 的硬件 I2C1（默认引脚，未重映射）。
 * 和本工程已占用的引脚不冲突：
 *     PA0      -> count_sensor (EXTI0)
 *     PB1、PB5 -> encoder      (EXTI1 / EXTI9_5)
 *
 * 屏幕按 8x16 字符划分：4 行 x 16 列，行列号都从 1 开始（line 1~4, column 1~16）。
 *
 * 用法：
 *     OLED_Init();                              // 上电初始化（内部已清屏）
 *     OLED_ShowString(1, 1, "count:");          // 打字符串
 *     OLED_ShowSignedNum(2, 1, encoder.data, 5);// 打带符号数字
 *     OLED_Printf(3, 1, "adc=%u", value);       // 格式化打印（见下方说明）
 * 也可以用结构体形式，风格和 encoder / count_sensor 一致：
 *     oled.init();
 *     oled.printf(1, 1, "cnt=%d", n);
 * =========================================================================== */

/* SSD1306 从机地址（已左移的 8 位形式，SA0 接地 = 0x78）。
 * 少数模块背面把 SA0 焊到了 VCC，那就改成 0x7A。 */
#define OLED_I2C_ADDRESS 0x78

#define OLED_WIDTH        128 /* 屏幕像素宽 */
#define OLED_HEIGHT       64  /* 屏幕像素高 */
#define OLED_TEXT_LINES   4   /* 8x16 字体下的文本行数 */
#define OLED_TEXT_COLUMNS 16  /* 8x16 字体下的每行字符数 */

/* 初始化 I2C1 + SSD1306，并清屏。可以重复调用（掉线后用它重连）。 */
void OLED_Init(void);

/* 全屏清空 / 清掉某一行 (line: 1~4) */
void OLED_Clear(void);
void OLED_ClearLine(uint8_t line);

/* 显示一个字符 / 一个字符串。
 * 超出右边界的部分直接截断，不会换行、不会绕回。
 * 字库只有可打印 ASCII (0x20~0x7E)，其它字符显示成 '?'。 */
void OLED_ShowChar(uint8_t line, uint8_t column, char ch);
void OLED_ShowString(uint8_t line, uint8_t column, const char *string);

/* 显示数字，length = 显示几位（右对齐、左边补 0，位数不够时高位被截掉）。
 * ShowSignedNum 会先打一个 '+'/'-' 号，再打 length 位数字（共占 length+1 列）。 */
void OLED_ShowNum(uint8_t line, uint8_t column, uint32_t number, uint8_t length);
void OLED_ShowSignedNum(uint8_t line, uint8_t column, int32_t number, uint8_t length);
void OLED_ShowHexNum(uint8_t line, uint8_t column, uint32_t number, uint8_t length);
void OLED_ShowBinNum(uint8_t line, uint8_t column, uint32_t number, uint8_t length);

/* 格式化打印，最多输出 16 个字符（多余的截断）。
 * 注意：走的是 newlib-nano 的 vsnprintf，默认不支持 %f；
 *       要打浮点得在 CMakeLists 的 target_link_options 里加 -u _printf_float。
 *       只有真正调用了本函数，printf 那一套才会被链进来（约几 KB Flash）。 */
void OLED_Printf(uint8_t line, uint8_t column, const char *format, ...)
    __attribute__((format(printf, 3, 4)));

/* 上一次 I2C 传输是否成功：1 = 屏在线，0 = 超时/没应答（没插屏、接错线、地址不对）。
 * 传输失败不会卡死程序，只会白等一个超时，然后自动复位 I2C 重试。 */
uint8_t OLED_IsOnline(void);

typedef struct _OLED {
    void (*init)(void);
    void (*clear)(void);
    void (*clear_line)(uint8_t line);
    void (*show_char)(uint8_t line, uint8_t column, char ch);
    void (*show_string)(uint8_t line, uint8_t column, const char *string);
    void (*show_num)(uint8_t line, uint8_t column, uint32_t number, uint8_t length);
    void (*show_signed_num)(uint8_t line, uint8_t column, int32_t number, uint8_t length);
    void (*show_hex_num)(uint8_t line, uint8_t column, uint32_t number, uint8_t length);
    void (*show_bin_num)(uint8_t line, uint8_t column, uint32_t number, uint8_t length);
    void (*printf)(uint8_t line, uint8_t column, const char *format, ...);
    uint8_t (*is_online)(void);
} OLED;

extern OLED oled;

#endif
