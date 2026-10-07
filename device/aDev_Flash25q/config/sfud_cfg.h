#ifndef PROJECT_SFUD_CFG_H
#define PROJECT_SFUD_CFG_H

#define SFUD_DEBUG_MODE
#define SFUD_DEBUG(...) ((void)0)
#define SFUD_USING_SFDP
#define SFUD_USING_FLASH_INFO_TABLE
/* 各设备独立初始化，不对外暴露官方设备表。 */
#define SFUD_FLASH_DEVICE_TABLE {{.name = "unused"}}
/* 不使用全局日志缓冲区，不隐式依赖控制台。 */
static inline void aDevFlash25qSfudLog(const char *format, ...)
{
    (void)format;
}
#define SFUD_INFO(...) aDevFlash25qSfudLog(__VA_ARGS__)

#endif
