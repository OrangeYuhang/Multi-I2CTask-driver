#ifndef _I2C_DEVICE_H_
#define _I2C_DEVICE_H_
#include <stdint.h>

/* 软 I2C 上的设备调度层：一主对多从的最小协作式调度。
 *
 * 模型：
 *   - 队列节点就是设备自己（内嵌的 I2CDevice base）。一次事件的工作
 *     （比如 GY521 的一次采样 = 读 INT_STATUS + 读 14 字节 + 解析）
 *     对应一次入队，update 返回 TASK_DONE 就出队。
 *   - 主循环按固定顺序扫描各设备的标志位，扫到就 I2CDevice_Enqueue()。
 *     扫描顺序就是隐式优先级：先扫到的设备先入队。
 *   - 主循环每轮调 I2CDevice_Run()：取队头设备，必要时切 TIM 频率，
 *     然后经 vptr 调 update。返回 TASK_DONE 就出队。
 *
 * 接口约定（vtable 式，编译器不检查）：
 *   设备结构体（GY521）的第一个成员必须是 I2CDevice base；
 *   base 的第一个成员必须是 vptr。两处顺序放错都是静默读垃圾，
 *   没有编译错误 —— 这是这个模式的固有代价，别在它们前面加字段。
 *   好处是双向 cast 都有标准背书（首成员地址恒等，6.7.2.1p15）：
 *   驱动侧 (GY521 *)this 下转、入队侧 &gy521.base 上转都不移动指针。
 *
 * step 契约：
 *   任务的步进状态住在设备里（update 只拿到 this，够不到队列），
 *   runtime 无法在入队时复位 —— 回调必须在每一条 TASK_DONE 返回
 *   路径上自己把 step 复位，漏掉任何一条都会让下一次任务从半路开始。
 *
 * DONE 契约：
 *   update 只有在"所有事务完成且 SoftI2C_Busy() == 0"时才允许返回
 *   TASK_DONE。runtime 不检查，靠调用方自觉 —— 否则下一个设备会在
 *   引擎还忙的时候接棒，第一笔提交就吃 ERR_BUSY。
 */

#define TASK_CONTINUE 0U
#define TASK_DONE     1U

typedef struct I2CDevice I2CDevice;

typedef struct I2CDeviceVTable {
    uint8_t (*update)(I2CDevice *this);
} I2CDeviceVTable;

struct I2CDevice {
    const I2CDeviceVTable *vptr; 
    uint32_t freq_hz;            
    uint8_t  active;             
    I2CDevice *next;         
};

void I2CDevice_Enqueue(I2CDevice *d);

/* runtime：主循环每轮调一次。队空则什么都不做。 */
void I2CDevice_Run(void);

#endif
