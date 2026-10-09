#ifndef APP_IDU_SIG_TABLE_H
#define APP_IDU_SIG_TABLE_H

#include "aBus.h"
#include <stdint.h>
#include "IDU_sig_ids.h"

/* 静态对象按应用设备号与下标注册，由 aBus 统一收集。
 * 绑定只关联存储，直接访问绑定变量的并发同步由应用自行决定。
 */
#define IDU_SIG_BIND(name, index, object) \
    ABUS_RAM_BIND_EXPORT(name, PROTOCOL_BUS_INSTANCE_ID, IDU_SIG_DEVICE_ID, \
                        index, object)

/* 填充 IDU 表描述，不创建 aBus；由 protocol 在启动时统一装配。
 * table 及其引用的数据在 aBus 使用期间须持续有效，不导出业务数据地址。
 */
aStatus_t IDUSigTableInit(aBusTable_t *table);

#endif
