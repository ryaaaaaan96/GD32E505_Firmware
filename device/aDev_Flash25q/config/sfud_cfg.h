#ifndef PROJECT_SFUD_CFG_H
#define PROJECT_SFUD_CFG_H

#define SFUD_DEBUG_MODE
#define SFUD_DEBUG(...) ((void)0)
#define SFUD_USING_SFDP
#define SFUD_USING_FLASH_INFO_TABLE
/* Objects are initialized individually; the upstream table is not exported. */
#define SFUD_FLASH_DEVICE_TABLE {{.name = "unused"}}
/* No global log buffer and no implicit console dependency. */
static inline void aDevFlash25qSfudLog(const char *format, ...)
{
    (void)format;
}
#define SFUD_INFO(...) aDevFlash25qSfudLog(__VA_ARGS__)

#endif
