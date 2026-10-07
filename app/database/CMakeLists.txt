# 本产品数据库使用板载 Flash，库本身不依赖具体设备。
set(app_database_enable OFF)
if(ADATABASE_ENABLE AND ADEV_FLASH25Q_ENABLE)
    set(app_database_enable ON)
    target_sources(${APP_TARGET} PRIVATE app_database.c)
    target_include_directories(${APP_TARGET} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${ACLASS_PRODUCT_DIR}/config")
    target_link_libraries(${APP_TARGET} PRIVATE aDataBase)
    if(ASHELL_ENABLE)
        target_sources(${APP_TARGET} PRIVATE app_database_command.c)
    endif()
endif()
target_compile_definitions(${APP_TARGET} PRIVATE
    APP_DATABASE_ENABLE=$<BOOL:${app_database_enable}>)
