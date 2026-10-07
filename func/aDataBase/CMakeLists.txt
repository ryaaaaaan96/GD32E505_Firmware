if(NOT ADATABASE_LAYOUT_FILE OR NOT EXISTS "${ADATABASE_LAYOUT_FILE}")
    message(FATAL_ERROR "aDataBase requires product ADATABASE_LAYOUT_FILE")
endif()

set(layout_dir "${CMAKE_CURRENT_BINARY_DIR}/include")
configure_file("${ADATABASE_LAYOUT_FILE}"
    "${layout_dir}/aDatabase_flash_layout.h" COPYONLY)

# 官方 FlashDB 和 FAL 原样编译，不编译示例和文件存储后端。
add_library(aDataBaseFlashDb OBJECT
    FlashDB/src/fdb.c
    FlashDB/src/fdb_kvdb.c
    FlashDB/src/fdb_tsdb.c
    FlashDB/src/fdb_utils.c
    FlashDB/port/fal/src/fal.c
    FlashDB/port/fal/src/fal_flash.c
    FlashDB/port/fal/src/fal_partition.c
)
target_include_directories(aDataBaseFlashDb PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/config"
    "${layout_dir}"
    "${CMAKE_CURRENT_SOURCE_DIR}/FlashDB/inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/FlashDB/port/fal/inc"
)
target_link_libraries(aDataBaseFlashDb PRIVATE aclass_project_options)
# 官方 FAL 模式不使用文件同步参数；仅对上游目标关闭此项告警。
target_compile_options(aDataBaseFlashDb PRIVATE -Wno-unused-parameter)

add_library(aDataBase STATIC aDataBase.c port/fal_storage_port.c)
target_include_directories(aDataBase PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(aDataBase PUBLIC aLib aDataBaseFlashDb
    PRIVATE aOS aclass_project_options)
foreach(feature STATIC DYNAMIC)
    target_compile_definitions(aDataBase PUBLIC
        ADATABASE_${feature}_ENABLE=$<BOOL:${ADATABASE_${feature}_ENABLE}>
    )
endforeach()

if(ADATABASE_BACKEND STREQUAL "FLASH25Q")
    add_library(aDataBaseFlash25q STATIC backend/flash25q/aDataBase_flash25q.c)
    target_include_directories(aDataBaseFlash25q PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/backend/flash25q"
    )
    target_link_libraries(aDataBaseFlash25q PUBLIC aDataBase aFlash25q
        PRIVATE aclass_project_options)
endif()
