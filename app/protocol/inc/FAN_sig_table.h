#ifndef APP_FAN_SIG_TABLE_H
#define APP_FAN_SIG_TABLE_H

#include "aBus.h"
#include "FAN_sig_ids.h"

typedef struct {
    uint32_t speed;
    int32_t temperature;
} FANMotor_t;

#define FAN_SIG_BIND(name, index, object) \
    ABUS_RAM_BIND_EXPORT(name, PROTOCOL_BUS_INSTANCE_ID, FAN_SIG_DEVICE_ID, \
                        index, object)

/* 填充 FAN 表描述；protocol 挂载后保持有效，字段定义与绑定由本模块持有。 */
aStatus_t FANSigTableInit(aBusTable_t *table);

#endif
