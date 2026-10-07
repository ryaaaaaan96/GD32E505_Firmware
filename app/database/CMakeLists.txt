# 当前应用实例使用板上的 Flash25q；自定义后端由对应产品自行编排。
set(app_database_enable OFF)
if(ADATABASE_ENABLE AND ADATABASE_BACKEND STREQUAL "FLASH25Q")
    set(app_database_enable ON)
    target_sources(${APP_TARGET} PRIVATE app_database.c)
    target_include_directories(${APP_TARGET} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(${APP_TARGET} PRIVATE aDataBaseFlash25q)
    if(ASHELL_ENABLE)
        target_sources(${APP_TARGET} PRIVATE app_database_command.c)
    endif()
endif()
target_compile_definitions(${APP_TARGET} PRIVATE
    APP_DATABASE_ENABLE=$<BOOL:${app_database_enable}>)
