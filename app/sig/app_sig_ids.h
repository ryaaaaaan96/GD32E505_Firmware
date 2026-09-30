#ifndef APP_SIG_IDS_H
#define APP_SIG_IDS_H

/* 本应用表的逻辑设备号，分散绑定与表定义共用。 */
enum {
    APP_SIG_INSTANCE_ID = 1U,
    APP_SIG_DEVICE_ID = 1U
};

/* 测点下标与表内顺序一致，用于快速访问；不作为持久化标识。 */
typedef enum {
    APP_BUS_MOTOR,
    APP_BUS_COUNTER,
    APP_BUS_SIG_COUNT
} appBusSigIndex_t;

/* Key 独立于数组顺序，实际产品发布后应保持稳定。 */
enum {
    APP_BUS_MOTOR_KEY = 1001U,
    APP_BUS_COUNTER_KEY = 42U
};

#endif
