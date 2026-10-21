#ifndef _I2C_TASK_H_
#define _I2C_TASK_H_
#include <stdint.h>

/* 软 I2C 上的任务调度层（static 实例版）：一主对多从的最小协作式调度。
 *
 * 模型：
 *   - 任务 = 一次事件的工作（有限步），由驱动文件里的 file-scope
 *     static 状态支撑。队列节点 I2CTask 只装「怎么推进」和队列簿记。
 *   - "什么时候入队"由驱动自己的 poll 回调决定。主循环调
 *     I2CTask_Poll() 逐个问注册过的任务，谁就绪就入谁。
 *     注册顺序就是隐式优先级：先注册的任务先被问。
 *   - 主循环每轮调 I2CTask_Run()：取队头任务，必要时切 TIM 频率，
 *     调 t->update()。返回 TASK_DONE 就出队。
 *
 * 单实例限制：
 *   每个驱动文件的 update 及其状态（step、事务缓冲）都是 file-scope
 *   static，所以同一类设备只能有一个实例。要两个同型号器件就得复制
 *   整个文件。对比 vtable 版（master 分支）的多实例能力，这是本设计
 *   的取舍点。
 *
 * step 契约：
 *   步进状态是驱动的 file-scope static，runtime 够不到 —— 回调必须
 *   在每一条 TASK_DONE 返回路径上自己把 step 复位，漏掉任何一条都会
 *   让下一次任务从半路开始。
 *
 * DONE 契约：
 *   update 只有在"所有事务完成且 SoftI2C_Busy() == 0"时才允许返回
 *   TASK_DONE。runtime 不检查，靠调用方自觉 —— 否则下一个任务会在
 *   引擎还忙的时候接棒，第一笔提交就吃 ERR_BUSY。
 */

#define TASK_CONTINUE 0U
#define TASK_DONE     1U

typedef struct I2CTask I2CTask;
struct I2CTask {
    uint8_t  (*update)(void); /* 步进回调：驱动 static 函数，无参数 */
    uint8_t  (*poll)(void);   /* 纯谓词：现在有没有工作要入队。不消费标志、
                               * 不入队，只回答 0/1 —— 消费标志是 update 的
                               * 职责，入队是 runtime 的职责。 */
    uint32_t freq_hz;         /* 本任务事务要求的 SCL 频率 */
    uint8_t  active;          /* 已在队列/执行中。扫描去重用 */
    I2CTask *next;            /* 队列链接 */
};

/* 入队（内部去重：active 的任务会被忽略）。任务对象是驱动文件里的
 * static 实例，队列只存指针。 */
void I2CTask_Enqueue(I2CTask *t);

/* 注册进调度器（驱动的 Init 里调用）。注册表容量见 i2c_task.c。 */
void I2CTask_Register(I2CTask *t);

/* 逐个问注册表里的任务，poll 返回就绪的就入队（内部去重）。 */
void I2CTask_Poll(void);

/* runtime：主循环每轮调一次。队空则什么都不做。 */
void I2CTask_Run(void);

#endif
