#ifndef _SYSTICK_H_
#define _SYSTICK_H_

#include <stdint.h>

/* 1ms 系统时基，基于 Cortex-M3 内核的 SysTick 定时器（不占用任何 TIMx）。
 *
 * 中断服务函数按本工程约定放在 User/stm32f10x_it.c 的 SysTick_Handler 里，
 * 那里只调一句 SysTick_IncTick()。
 *
 * 注意：SysTick 被设成最低优先级，所以它不会打断 EXTI 中断 ——
 * 这也保证了在 EXTI 里读 millis() 不会读到撕裂的中间值。 */

/* 启动 1ms 时基。可重复调用（会重新装载计数器）。 */
void SysTick_Init(void);

/* 供 SysTick_Handler 调用，累加毫秒计数 */
void SysTick_IncTick(void);

/* 开机以来的毫秒数，约 49.7 天回绕。
 * 用无符号减法比较时间差 (millis() - start < N) 可以正确跨越回绕点。 */
uint32_t millis(void);

/* 阻塞延时。delay_ms 要先把 1ms 时基跑起来，所以必须先调用 SysTick_Init()；
 * delay_us 走 DWT 周期计数器，不依赖时基，但精度取决于 SystemCoreClock。 */
void delay_ms(uint32_t ms);
void delay_us(uint32_t us);

#endif
