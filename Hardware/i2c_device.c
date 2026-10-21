#include "i2c_device.h"
#include <stddef.h>
#include "soft_i2c.h"

typedef struct {
    I2CDevice *head;
    I2CDevice *tail;
} I2CQueue;

static I2CQueue s_q;
#define I2C_DEVICE_MAX 8
static I2CDevice *s_registry[I2C_DEVICE_MAX];
static uint8_t s_count = 0U;

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

void I2CDevice_Register(I2CDevice *d)
{
    if (d != NULL && s_count < I2C_DEVICE_MAX) {
        s_registry[s_count++] = d;
    }
}

void I2CDevice_Poll(void)
{
    for (uint8_t i = 0U; i < s_count; i++) {
        if (s_registry[i]->vptr->poll(s_registry[i])) {
            I2CDevice_Enqueue(s_registry[i]);
        }
    }
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
