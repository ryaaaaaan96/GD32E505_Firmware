/* 自动生成：只读定义和静态兜底存储，不创建任务或总线实例。 */
% for device in devices:
#include "../inc/${device["name"]}_sig_table.h"
% endfor

#define ABUS_BOUND_SIG(...) ABUS_SIG(__VA_ARGS__)
#define ABUS_SIG(...)
#define ABUS_PARAM(name, ...) [name] = {__VA_ARGS__},
#define ABUS_PARAMS(group, ...) ${chr(92)}
    static const aBusParam_t group##_params[] = {__VA_ARGS__};
% for device in devices:
#include "../sig/${device["name"]}_test.inc"
% endfor
#undef ABUS_PARAMS
#undef ABUS_PARAM
#undef ABUS_SIG

#define ABUS_PARAMS(...)
#define ABUS_PARAM_REF(group) ${chr(92)}
    .params = group##_params, ${chr(92)}
    .param_count = sizeof(group##_params) / sizeof(group##_params[0])
#define ABUS_SIG(name, key, value_type, ...) ${chr(92)}
    {.sigKey = (key), .size = sizeof(value_type), __VA_ARGS__},
% for device in devices:
static const aBusSig_t ${device["name"]}_sigs[] = {
#include "../sig/${device["name"]}_test.inc"
};
% endfor
#undef ABUS_SIG
#undef ABUS_PARAM_REF
#undef ABUS_PARAMS
#undef ABUS_BOUND_SIG

static const aBusTable_t tables[] = {
% for device in devices:
    {.sigs = ${device["name"]}_sigs, .sig_count = ${device["name"]}_SIG_COUNT,
     .deviceID = ${device["name"]}_SIG_DEVICE_ID},
% endfor
};

#if !ABUS_DYNAMIC_ENABLE
/* 已声明 bound 的项仍由应用持有，不重复注册。 */
#define ABUS_BOUND_SIG(...)
#define ABUS_PARAMS(...)
% for device in devices:
#define ABUS_SIG(name, key, value_type, ...) ${chr(92)}
    static value_type ${device["name"]}_##name##_storage; ${chr(92)}
    ABUS_RAM_BIND_EXPORT(${device["name"]}_##name##_binding, ${chr(92)}
        PROTOCOL_BUS_INSTANCE_ID, ${device["name"]}_SIG_DEVICE_ID, ${chr(92)}
        ${device["name"]}_SIG_##name, ${device["name"]}_##name##_storage);
#include "../sig/${device["name"]}_test.inc"
#undef ABUS_SIG
% endfor
#undef ABUS_PARAMS
#undef ABUS_BOUND_SIG
#endif
