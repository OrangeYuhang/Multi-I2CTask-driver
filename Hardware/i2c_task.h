#ifndef _I2C_TASK_H_
#define _I2C_TASK_H_
#include <stdint.h>
#define TASK_CONTINUE 0U
#define TASK_DONE     1U

typedef struct I2CTask I2CTask;
struct I2CTask {
    uint8_t  dev;                       /* 从机 7 位地址 */
    uint32_t freq_hz;                   /* 本任务事务要求的 SCL 频率 */
    uint8_t (*update)(I2CTask *self);   /* 步进回调，runtime 每轮调一次 */
    uint8_t  active;                    /* 已在队列/执行中。扫描去重用 */
    uint8_t  step;                      /* 回调自己的步进位置，入队时清零 */
    uint8_t  err_count;                 /* 本任务累计 I2C 失败次数 */
    I2CTask *next;

    /* 驱动私有数据。回调里取回自己的驱动结构体：
     *     GY521 *g = (GY521 *)self->ctx;  */
    void    *ctx;
};

void I2CTask_Enqueue(I2CTask *t);
void I2CTask_Run(void);

#endif
