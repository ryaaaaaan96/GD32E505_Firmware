if(ADEV_FLASH25Q_ENABLE AND ASHELL_ENABLE)
    target_sources(${APP_TARGET} PRIVATE flash_test.c)
endif()
