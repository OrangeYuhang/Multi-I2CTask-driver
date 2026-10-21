#include "i2c_device.h"
#include <stddef.h>
#include "soft_i2c.h"

typedef struct {
    I2CDevice *head;
    I2CDevice *tail;
} I2CQueue;

static I2CQueue s_q;

static uint32_t s_cur_freq = 0U;

void I2CDevice_Enqueue(I2CDevice *d)
{
    if (d == NULL || d->active) {
        return;
    }

    d->active = 1U;
    d->next   = NULL;

    if (s_q.tail != NULL) {
        s_q.tail->next = d;
    } else {
        s_q.head = d;
    }
    s_q.tail = d;
}

void I2CDevice_Run(void)
{
    I2CDevice *d = s_q.head;

    if (d == NULL) {
        return;
    }

    if (!SoftI2C_Busy() && d->freq_hz != s_cur_freq) {
        SoftI2C_SetSpeed(d->freq_hz);
        s_cur_freq = d->freq_hz;
    }

    if (d->vptr->update(d) == TASK_DONE) {
        s_q.head = d->next;
        if (s_q.head == NULL) {
            s_q.tail = NULL;
        }
        d->active = 0U;
    }
}
