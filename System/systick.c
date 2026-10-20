#include "systick.h"
#include "stm32f10x.h"

/* 这个版本的 core_cm3.h（CMSIS 1.30）没有带 DWT 的定义，自己补上，
 * 地址取自 Cortex-M3 的 TRM。CYCCNT 是内核的周期计数器：
 * 72MHz 下数一个数就是 1/72 微秒，用它算微秒级延时是准的。 */
#define DWT_CTRL           (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT         (*(volatile uint32_t *)0xE0001004u)
#define DWT_CTRL_CYCCNTENA (1u << 0)

static volatile uint32_t systick_ms = 0;

void SysTick_Init(void)
{
    /* delay_us 靠 DWT 的周期计数器计时，先把它打开。
     * TRCENA 是调试单元的总开关，不开的话 DWT 根本不计数。 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT_CYCCNT = 0;
    DWT_CTRL |= DWT_CTRL_CYCCNTENA;

    /* 这个版本的 core_cm3.h（CMSIS 1.30）还没有 SysTick_Config() helper，
     * 直接写寄存器。72MHz 下重装载值 = 72000 - 1，正好 1ms。 */
    SysTick->LOAD = (SystemCoreClock / 1000u) - 1u;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk /* 时钟源 = AHB（72MHz），不 8 分频 */
                    | SysTick_CTRL_TICKINT_Msk /* 计到 0 时产生中断 */
                    | SysTick_CTRL_ENABLE_Msk;

    /* 设成最低优先级：时基不该抢占任何业务中断 */
    NVIC_SetPriority(SysTick_IRQn, (1u << __NVIC_PRIO_BITS) - 1u);
}

void SysTick_IncTick(void)
{
    systick_ms++;
}

uint32_t millis(void)
{
    return systick_ms;
}

void delay_ms(uint32_t ms)
{
    uint32_t start = millis();

    while (millis() - start < ms) {
    }
}
void delay_us(uint32_t us)
{
    /* DWT 是在 SysTick_Init() 里打开的。万一有谁在那之前就调到这儿，
     * CYCCNT 根本不动，下面的循环会永远转下去 —— 与其留这么个坑，
     * 不如在这里自己补一次使能，让 delay_us 不依赖调用顺序。 */
    if ((DWT_CTRL & DWT_CTRL_CYCCNTENA) == 0u) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT_CTRL |= DWT_CTRL_CYCCNTENA;
    }

    uint32_t start = DWT_CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000u);

    /* CYCCNT 是自由运行的 32 位计数器，72MHz 下约 60 秒回绕一次。
     * 这里只关心"过了多少个周期"，用无符号减法比较，回绕也是对的。
     * 别改成 for 空循环：一来循环体的周期数要手工数（这个函数原来就是
     * 按 1 周期/次算的，实际是 10 周期，慢了 10 倍），二来换优化等级
     * 结果就变了。 */
    while (DWT_CYCCNT - start < ticks) {
    }
}
