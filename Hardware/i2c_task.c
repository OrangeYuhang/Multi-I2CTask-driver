#include "i2c_task.h"
#include <stddef.h>
#include "soft_i2c.h"

typedef struct {
    I2CTask *head;
    I2CTask *tail;
} I2CQueue;

static I2CQueue s_q;

/* 注册表：全体任务名单。poll 要遍历的是"所有任务"，
 * 而队列里只有活跃的那几个，所以必须单独记一份。 */
#define I2C_TASK_MAX 8
static I2CTask *s_registry[I2C_TASK_MAX];
static uint8_t s_count = 0U;

/* 0 = 尚未为任何任务切过速。第一个任务到队头时必然触发一次切换，
 * 即使它的 freq 和引擎默认值相同 —— 重复写一次 ARR 无害。 */
static uint32_t s_cur_freq = 0U;

void I2CTask_Enqueue(I2CTask *t)
{
    if (t == NULL || t->active) {
        return;
    }

    t->active = 1U;
    t->next   = NULL;

    if (s_q.tail != NULL) {
        s_q.tail->next = t;
    } else {
        s_q.head = t;
    }
    s_q.tail = t;
}

void I2CTask_Register(I2CTask *t)
{
    if (t != NULL && s_count < I2C_TASK_MAX) {
        s_registry[s_count++] = t;
    }
}

void I2CTask_Poll(void)
{
    for (uint8_t i = 0U; i < s_count; i++) {
        if (s_registry[i]->poll()) {
            I2CTask_Enqueue(s_registry[i]); /* active 去重在 Enqueue 里 */
        }
    }
}

void I2CTask_Run(void)
{
    I2CTask *t = s_q.head;

    if (t == NULL) {
        return;
    }

    if (!SoftI2C_Busy() && t->freq_hz != s_cur_freq) {
        SoftI2C_SetSpeed(t->freq_hz);
        s_cur_freq = t->freq_hz;
    }

    if (t->update() == TASK_DONE) {
        s_q.head = t->next;
        if (s_q.head == NULL) {
            s_q.tail = NULL;
        }
        t->active = 0U;
    }
}
