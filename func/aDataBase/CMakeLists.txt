if(NOT ADATABASE_LAYOUT_FILE OR NOT EXISTS "${ADATABASE_LAYOUT_FILE}")
    message(FATAL_ERROR "aDataBase requires product ADATABASE_LAYOUT_FILE")
endif()
set(layout_dir "${CMAKE_CURRENT_BINARY_DIR}/include")
configure_file("${ADATABASE_LAYOUT_FILE}"
    "${layout_dir}/aDatabase_flash_layout.h" COPYONLY)

set(FLASHDB_SOURCES
    flashDB/src/fdb.c
    flashDB/src/fdb_kvdb.c
    flashDB/src/fdb_utils.c
)

add_library(
    aDataBase
    STATIC
        ${FLASHDB_SOURCES}
        port/fal_storage_port.c
)

target_include_directories(
    aDataBase
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/port/include"
        "${layout_dir}"
        "${CMAKE_CURRENT_SOURCE_DIR}/flashDB/inc"
)

target_link_libraries(
    aDataBase
    PUBLIC
        aLib
)

if(ADATABASE_BACKEND STREQUAL "FLASH25Q")
    add_library(aDataBaseFlash25q STATIC backend/flash25q/aDataBase_flash25q.c)
    target_include_directories(aDataBaseFlash25q PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/backend/flash25q"
    )
    target_link_libraries(aDataBaseFlash25q PUBLIC aDataBase aFlash25q
        PRIVATE aclass_project_options)
endif()

# FlashDB 是上游代码，不使用本工程对自有代码开启的严格告警升级。
set_source_files_properties(
    ${FLASHDB_SOURCES}
    PROPERTIES
        COMPILE_OPTIONS "-Wno-error;-Wno-pedantic;-Wno-unused-parameter"
)
target_link_libraries(aDataBase PRIVATE aclass_project_options)
