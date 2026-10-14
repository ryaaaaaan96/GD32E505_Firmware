/**
 * @file system_init.h
 * @brief 应用基础服务的统一启动入口与通用数据访问。
 * @see docs/interface_contract.md 公共类型、错误、超时与生命周期约定。
 *
 * 由 main 在驱动和 aOS 初始化后调用；业务任务仍归 APP 所有。
 */

#ifndef APP_SYSTEM_INIT_H
#define APP_SYSTEM_INIT_H

#include "aStatus.h"
#if ABUS_ENABLE
#include "aBus.h"
#endif
#if APP_DATABASE_ENABLE
#include "aDataBase.h"
#endif

/**
 * @brief 按顺序初始化状态灯、Shell、存储、日志、总线、数据库和协议服务。
 * @return 全部成功为 A_STATUS_OK，否则返回初始化错误；
 * 若当前初始化步骤的资源清理也失败，优先返回清理错误。
 * @warning 启动阶段单线程调用一次；失败不回滚已经创建的 status 任务。
 * 失败进入启动停止流程，不支持重试；已创建的 Shell 任务保持等待，不处理命令。
 * @note 控制台串口由 Shell 与日志共享，先于两者初始化；任务均由应用创建。
 */
aStatus_t aSystemInit(void);

/* 系统实例均由本模块私有持有，业务不得销毁；不提供运行时总线重建。 */
#if ABUS_ENABLE
/* 仅任务上下文；未初始化返回 NOT_READY。查询不暴露 handle 或数据地址。
 * Shell 等外部入口先解析稳定键，业务可直接使用枚举下标。
 */
aStatus_t dataBusResolveKey(const aBusSigKeyQuery_t *query,
    size_t *sigIndex);
aStatus_t dataBusGetInfo(const aBusSigQuery_t *query, aBusSigInfo_t *info);

/* 按下标读写整组，长度必须完全一致；请求和缓冲区仅在调用期间借用。
 * timeout 仅控制 aBus 锁等待，不保护直接访问绑定变量的代码。
 */
aStatus_t dataBusSet(const aBusSetIndexRequest_t *request);
aStatus_t dataBusGet(const aBusGetIndexRequest_t *request);

/* STRUCT 字段访问，共用 SIG 锁和范围规则，其他字段保持原值。 */
aStatus_t dataBusSetParam(const aBusSetParamRequest_t *request);
aStatus_t dataBusGetParam(const aBusGetParamRequest_t *request);

#endif

#if APP_DATABASE_ENABLE
#if ABUS_ENABLE
/* 显式保存/恢复。NULL 处理全部持久化项；单项按 deviceID + sigKey。
 * 当前应用工作区支持不超过 256 字节的 SIG；各项独立提交。
 * 写值不会自动写 Flash，TEST_001 每秒递增也不会产生 Flash 写入。
 * 恢复时旧记录长度不符则逻辑删除，保留当前 RAM，启动时即保留默认值。
 * 单项缺失/已作废返回 NOT_FOUND；批量恢复跳过该项，继续恢复其他项。
 * 范围、读取或删除失败仍返回错误。应用串行化保存/恢复操作。
 */
aStatus_t dataBusSave(const aBusSigKeyQuery_t *key);
aStatus_t dataBusRestore(const aBusSigKeyQuery_t *key);
#endif
/* Open/Close 由应用串行化，并保证没有正在执行的数据库读写。 */
/** 显式允许首次格式化或修复，仅作用于配置中指定的数据库分区。 */
aStatus_t appDatabaseOpen(aBool_t format_if_needed);
aStatus_t appDatabaseClose(void);
aStatus_t appDatabaseKvSet(const aDataBaseKvSetRequest_t *request);
aStatus_t appDatabaseKvGet(const aDataBaseKvGetRequest_t *request);
aStatus_t appDatabaseKvDelete(const aDataBaseKvDeleteRequest_t *request);
aStatus_t appDatabaseTsAppend(const aDataBaseTsAppendRequest_t *request);
aStatus_t appDatabaseTsIterate(const aDataBaseTsIterateRequest_t *request);
aStatus_t appDatabaseTsGetInfo(aDataBaseTsInfo_t *info);

#endif

#endif
