/* 测点清单：名称、固定 Key、描述。多次展开，不设置头文件保护。
 * 顺序决定 sigIndex；Key 发布后保持不变。Counter 的 RAM 属于测试任务。
 */
ABUS_SIG(COUNTER, 42U,
    .type = ALIB_DATA_U32,
    .flags = ABUS_SIG_FLAG_LOCK,
    .size = sizeof(uint32_t)
)
