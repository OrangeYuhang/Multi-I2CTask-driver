#include "i2c_task.h"
#include <stddef.h>
#include "soft_i2c.h"

typedef struct {
    I2CTask *head;
    I2CTask *tail;
} I2CQueue;

static I2CQueue s_q;

static uint32_t s_cur_freq = 0U;

void I2CTask_Enqueue(I2CTask *t)
{
    if (t == NULL || t->active) {
        return;
    }

    t->active = 1U;
    t->step   = 0U;
    t->next   = NULL;

    if (s_q.tail != NULL) {
        s_q.tail->next = t;
    } else {
        s_q.head = t;
    }
    s_q.tail = t;
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

    if (t->update(t) == TASK_DONE) {
        s_q.head = t->next;
        if (s_q.head == NULL) {
            s_q.tail = NULL;
        }
        t->active = 0U;
    }
}
