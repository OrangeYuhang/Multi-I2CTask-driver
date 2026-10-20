#include "oled.h"
#include "oled_font.h"
#include "stm32f10x.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_i2c.h"
#include "stm32f10x_rcc.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------- 硬件配置 ---------------------------------- */
#define OLED_I2C          I2C1
#define OLED_I2C_CLK      RCC_APB1Periph_I2C1
#define OLED_GPIO         GPIOB
#define OLED_GPIO_CLK     RCC_APB2Periph_GPIOB
#define OLED_SCL_PIN      GPIO_Pin_6
#define OLED_SDA_PIN      GPIO_Pin_7
#define OLED_I2C_SPEED    400000 /* SSD1306 支持 400kHz 快速模式 */

/* 等待 I2C 事件的自旋上限。屏没插时每次传输大约白等 1ms 左右就返回，
 * 不会像常见教程里那样 while 到天荒地老把整个程序卡死。 */
#define OLED_I2C_TIMEOUT  0x4000u

#define OLED_PAGES        (OLED_HEIGHT / 8) /* SSD1306 一页 8 行，共 8 页 */
#define OLED_FONT_WIDTH   8

/* SSD1306 I2C 控制字节：Co=0 时，后面所有字节同属一类 */
#define OLED_CTRL_CMD     0x00 /* 后续全是命令 */
#define OLED_CTRL_DATA    0x40 /* 后续全是显存数据 */

static volatile uint8_t oled_online = 0;

/* 整页清屏用的全 0 缓冲，放 Flash 里，省 RAM 也省一次 memset */
static const uint8_t oled_blank[OLED_WIDTH] = {0};

static void OLED_I2C_Configure(void);

/* ------------------------------- 粗延时 ------------------------------------ */
/* 只用于上电等待和手动敲总线时钟，不需要精确，所以不占用 SysTick 和定时器，
 * 免得跟以后自己写的 Delay 模块打架。 */
static void OLED_DelayUs(uint32_t us)
{
    volatile uint32_t cycles = us * (SystemCoreClock / 8000000u);
    while (cycles--) {
    }
}

static void OLED_DelayMs(uint32_t ms)
{
    volatile uint32_t cycles = ms * (SystemCoreClock / 8000u);
    while (cycles--) {
    }
}

/* ------------------------------- 总线自救 ---------------------------------- */
/* 调试时按复位键，很可能正好打断一次 I2C 传输：从机以为自己还在发数据，
 * 会一直把 SDA 拉低，于是 MCU 重启后 BUSY 永远置位，一个字节都发不出去。
 * 标准解法：先用普通开漏 GPIO 手动敲 9 个 SCL，把从机的位计数器走完，
 * 再补一个 STOP，让它回到空闲状态。之后再切成 I2C 复用功能。 */
static void OLED_I2C_BusRecover(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    uint8_t i;

    RCC_APB2PeriphClockCmd(OLED_GPIO_CLK, ENABLE);

    GPIO_InitStructure.GPIO_Pin = OLED_SCL_PIN | OLED_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(OLED_GPIO, &GPIO_InitStructure);

    GPIO_SetBits(OLED_GPIO, OLED_SCL_PIN | OLED_SDA_PIN);
    OLED_DelayUs(10);

    for (i = 0; i < 9; i++) {
        GPIO_ResetBits(OLED_GPIO, OLED_SCL_PIN);
        OLED_DelayUs(5);
        GPIO_SetBits(OLED_GPIO, OLED_SCL_PIN);
        OLED_DelayUs(5);
    }

    /* 补一个 STOP：SCL 为高时，SDA 由低跳高 */
    GPIO_ResetBits(OLED_GPIO, OLED_SDA_PIN);
    OLED_DelayUs(5);
    GPIO_SetBits(OLED_GPIO, OLED_SCL_PIN);
    OLED_DelayUs(5);
    GPIO_SetBits(OLED_GPIO, OLED_SDA_PIN);
    OLED_DelayUs(10);
}

/* ------------------------------ I2C 底层 ----------------------------------- */
static void OLED_I2C_Configure(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    I2C_InitTypeDef I2C_InitStructure;

    RCC_APB2PeriphClockCmd(OLED_GPIO_CLK, ENABLE);
    RCC_APB1PeriphClockCmd(OLED_I2C_CLK, ENABLE);

    /* I2C 是开漏总线，必须用复用开漏；模块自带上拉电阻，MCU 这边不用管 */
    GPIO_InitStructure.GPIO_Pin = OLED_SCL_PIN | OLED_SDA_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(OLED_GPIO, &GPIO_InitStructure);

    I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;
    I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_2;
    I2C_InitStructure.I2C_OwnAddress1 = 0x00; /* 只做主机，本机地址随便填 */
    I2C_InitStructure.I2C_Ack = I2C_Ack_Enable;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_InitStructure.I2C_ClockSpeed = OLED_I2C_SPEED;
    I2C_Init(OLED_I2C, &I2C_InitStructure);

    I2C_Cmd(OLED_I2C, ENABLE);
}

/* 出错收尾：发 STOP、清应答失败标志、软复位状态机，然后重新配置。
 * 不做软复位的话，一次 NACK 之后 BUSY 有可能一直挂着，后面全废。 */
static void OLED_I2C_Abort(void)
{
    I2C_GenerateSTOP(OLED_I2C, ENABLE);
    I2C_ClearFlag(OLED_I2C, I2C_FLAG_AF);

    I2C_SoftwareResetCmd(OLED_I2C, ENABLE);
    I2C_SoftwareResetCmd(OLED_I2C, DISABLE);
    OLED_I2C_Configure(); /* 软复位会把寄存器打回默认值，得重配 */

    oled_online = 0;
}

static uint8_t OLED_WaitEvent(uint32_t event)
{
    uint32_t timeout = OLED_I2C_TIMEOUT;

    while (I2C_CheckEvent(OLED_I2C, event) != SUCCESS) {
        if (--timeout == 0u) {
            return 0;
        }
    }
    return 1;
}

/* 一次完整传输：START -> 从机地址 -> 控制字节 -> length 个字节 -> STOP
 * 返回 1 成功、0 超时（超时后总线已被复位，下次调用可以直接重试）。 */
static uint8_t OLED_I2C_Write(uint8_t control, const uint8_t *buffer, uint16_t length)
{
    uint32_t timeout = OLED_I2C_TIMEOUT;
    uint16_t i;

    while (I2C_GetFlagStatus(OLED_I2C, I2C_FLAG_BUSY) == SET) {
        if (--timeout == 0u) {
            OLED_I2C_Abort();
            return 0;
        }
    }

    I2C_GenerateSTART(OLED_I2C, ENABLE);
    if (!OLED_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT)) {
        OLED_I2C_Abort();
        return 0;
    }

    /* 屏不在线时从机不应答，这里会等到超时（AF 置位），由 Abort 收拾 */
    I2C_Send7bitAddress(OLED_I2C, OLED_I2C_ADDRESS, I2C_Direction_Transmitter);
    if (!OLED_WaitEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)) {
        OLED_I2C_Abort();
        return 0;
    }

    I2C_SendData(OLED_I2C, control);
    if (!OLED_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTING)) {
        OLED_I2C_Abort();
        return 0;
    }

    for (i = 0; i < length; i++) {
        I2C_SendData(OLED_I2C, buffer[i]);
        /* 等 TXE：数据寄存器空了就能塞下一个字节，不用等移位器发完 */
        if (!OLED_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTING)) {
            OLED_I2C_Abort();
            return 0;
        }
    }

    /* 最后一个字节必须等 BTF（真正发完）再发 STOP，否则会把它截断 */
    if (!OLED_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED)) {
        OLED_I2C_Abort();
        return 0;
    }
    I2C_GenerateSTOP(OLED_I2C, ENABLE);

    oled_online = 1;
    return 1;
}

static void OLED_WriteCommands(const uint8_t *commands, uint16_t length)
{
    OLED_I2C_Write(OLED_CTRL_CMD, commands, length);
}

static void OLED_WriteData(const uint8_t *data, uint16_t length)
{
    OLED_I2C_Write(OLED_CTRL_DATA, data, length);
}

/* 定位光标到第 page 页、第 column 列（都从 0 开始，像素坐标）。
 * 页寻址模式下列指针会自动右移，所以定位一次就能连续写一整页。 */
static void OLED_SetCursor(uint8_t page, uint8_t column)
{
    uint8_t commands[3];

    commands[0] = (uint8_t)(0xB0 | (page & 0x07));         /* 设置页地址 */
    commands[1] = (uint8_t)(0x10 | ((column >> 4) & 0x0F)); /* 列地址高 4 位 */
    commands[2] = (uint8_t)(0x00 | (column & 0x0F));        /* 列地址低 4 位 */
    OLED_WriteCommands(commands, 3);
}

/* -------------------------------- 对外接口 --------------------------------- */
void OLED_Init(void)
{
    static const uint8_t init_commands[] = {
        0xAE,       /* 关显示（配置期间先黑屏） */
        0xD5, 0x80, /* 显示时钟分频 / 振荡频率 */
        0xA8, 0x3F, /* 多路复用比 = 1/64 */
        0xD3, 0x00, /* 显示偏移 = 0 */
        0x40,       /* 起始行 = 0 */
        0x8D, 0x14, /* 电荷泵使能（模块只有 3.3V 输入，必须开，否则不亮） */
        0x20, 0x02, /* 页寻址模式 */
        0xA1,       /* 左右方向：列 127 映射到 SEG0（改 0xA0 可左右翻转） */
        0xC8,       /* 上下方向：COM 反向扫描（改 0xC0 可上下翻转） */
        0xDA, 0x12, /* COM 引脚硬件配置 */
        0x81, 0xCF, /* 对比度 */
        0xD9, 0xF1, /* 预充电周期 */
        0xDB, 0x30, /* VCOMH 取消选择电平 */
        0xA4,       /* 显存内容正常输出（0xA5 = 强制全亮，可用来测屏） */
        0xA6,       /* 正常显示（0xA7 = 反色） */
        0xAF,       /* 开显示 */
    };

    OLED_DelayMs(100);     /* 等 SSD1306 内部上电复位完成 */
    OLED_I2C_BusRecover(); /* 先解可能存在的总线死锁 */
    OLED_I2C_Configure();

    OLED_WriteCommands(init_commands, sizeof(init_commands));
    OLED_Clear();
}

void OLED_Clear(void)
{
    uint8_t page;

    for (page = 0; page < OLED_PAGES; page++) {
        OLED_SetCursor(page, 0);
        OLED_WriteData(oled_blank, OLED_WIDTH);
    }
}

void OLED_ClearLine(uint8_t line)
{
    uint8_t half;

    if (line < 1 || line > OLED_TEXT_LINES) {
        return;
    }

    for (half = 0; half < 2; half++) {
        OLED_SetCursor((uint8_t)((line - 1) * 2 + half), 0);
        OLED_WriteData(oled_blank, OLED_WIDTH);
    }
}

void OLED_ShowString(uint8_t line, uint8_t column, const char *string)
{
    uint8_t buffer[OLED_WIDTH];
    uint8_t count = 0;
    uint8_t page, half, i;

    if (string == 0 || line < 1 || line > OLED_TEXT_LINES) {
        return;
    }
    if (column < 1 || column > OLED_TEXT_COLUMNS) {
        return;
    }

    /* 能显示几个字符：到字符串结尾或屏幕右边界为止 */
    while (string[count] != '\0' && (uint8_t)(column - 1 + count) < OLED_TEXT_COLUMNS) {
        count++;
    }
    if (count == 0) {
        return;
    }

    page = (uint8_t)((line - 1) * 2);

    /* 8x16 字符跨上下两页。按页把整串字模拼进缓冲区一次性发出去，
     * 这样一个字符串只需要 4 次 I2C 传输（2 次定位 + 2 次数据），
     * 比“一个字符一发”快十几倍。 */
    for (half = 0; half < 2; half++) {
        for (i = 0; i < count; i++) {
            uint8_t code = (uint8_t)string[i];

            if (code < 0x20 || code > 0x7E) {
                code = '?'; /* 字库外的字符（含中文、控制字符）统一显示问号 */
            }
            memcpy(&buffer[i * OLED_FONT_WIDTH],
                   &OLED_F8x16[(code - 0x20) * 16 + half * OLED_FONT_WIDTH],
                   OLED_FONT_WIDTH);
        }

        OLED_SetCursor((uint8_t)(page + half), (uint8_t)((column - 1) * OLED_FONT_WIDTH));
        OLED_WriteData(buffer, (uint16_t)count * OLED_FONT_WIDTH);
    }
}

void OLED_ShowChar(uint8_t line, uint8_t column, char ch)
{
    char text[2];

    text[0] = ch;
    text[1] = '\0';
    OLED_ShowString(line, column, text);
}

void OLED_ShowNum(uint8_t line, uint8_t column, uint32_t number, uint8_t length)
{
    char text[OLED_TEXT_COLUMNS + 1];
    uint8_t i;

    if (length == 0) {
        return;
    }
    if (length > OLED_TEXT_COLUMNS) {
        length = OLED_TEXT_COLUMNS;
    }

    /* 从个位往回填，高位不足补 '0' */
    for (i = 0; i < length; i++) {
        text[length - 1 - i] = (char)('0' + number % 10);
        number /= 10;
    }
    text[length] = '\0';

    OLED_ShowString(line, column, text);
}

void OLED_ShowSignedNum(uint8_t line, uint8_t column, int32_t number, uint8_t length)
{
    uint32_t magnitude;

    if (number < 0) {
        /* 先转 int64 再取负，避免 INT32_MIN 溢出（-(-2147483648) 是未定义行为） */
        magnitude = (uint32_t)(-(int64_t)number);
        OLED_ShowChar(line, column, '-');
    } else {
        magnitude = (uint32_t)number;
        OLED_ShowChar(line, column, '+');
    }

    OLED_ShowNum(line, (uint8_t)(column + 1), magnitude, length);
}

void OLED_ShowHexNum(uint8_t line, uint8_t column, uint32_t number, uint8_t length)
{
    char text[OLED_TEXT_COLUMNS + 1];
    uint8_t i, nibble;

    if (length == 0) {
        return;
    }
    if (length > OLED_TEXT_COLUMNS) {
        length = OLED_TEXT_COLUMNS;
    }

    for (i = 0; i < length; i++) {
        nibble = (uint8_t)(number & 0x0F);
        text[length - 1 - i] = (char)(nibble < 10 ? ('0' + nibble) : ('A' + nibble - 10));
        number >>= 4;
    }
    text[length] = '\0';

    OLED_ShowString(line, column, text);
}

void OLED_ShowBinNum(uint8_t line, uint8_t column, uint32_t number, uint8_t length)
{
    char text[OLED_TEXT_COLUMNS + 1];
    uint8_t i;

    if (length == 0) {
        return;
    }
    if (length > OLED_TEXT_COLUMNS) {
        length = OLED_TEXT_COLUMNS;
    }

    for (i = 0; i < length; i++) {
        text[length - 1 - i] = (char)('0' + (number & 0x01));
        number >>= 1;
    }
    text[length] = '\0';

    OLED_ShowString(line, column, text);
}

void OLED_Printf(uint8_t line, uint8_t column, const char *format, ...)
{
    char text[OLED_TEXT_COLUMNS + 1];
    va_list args;

    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    OLED_ShowString(line, column, text);
}

uint8_t OLED_IsOnline(void)
{
    return oled_online;
}

OLED oled = {
    .init = OLED_Init,
    .clear = OLED_Clear,
    .clear_line = OLED_ClearLine,
    .show_char = OLED_ShowChar,
    .show_string = OLED_ShowString,
    .show_num = OLED_ShowNum,
    .show_signed_num = OLED_ShowSignedNum,
    .show_hex_num = OLED_ShowHexNum,
    .show_bin_num = OLED_ShowBinNum,
    .printf = OLED_Printf,
    .is_online = OLED_IsOnline,
};
