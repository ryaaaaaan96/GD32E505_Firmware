set(FLASHDB_SOURCES
    flashDB/src/fdb.c
    flashDB/src/fdb_kvdb.c
    flashDB/src/fdb_utils.c
)

add_library(
    aDataBase
    STATIC
        ${FLASHDB_SOURCES}
)

target_include_directories(
    aDataBase
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/flashDB/inc"
)

target_link_libraries(
    aDataBase
    PUBLIC
        aMemory
)

# FlashDB 是上游代码，不使用本工程对自有代码开启的严格告警升级。
set_source_files_properties(
    ${FLASHDB_SOURCES}
    PROPERTIES
        COMPILE_OPTIONS "-Wno-error;-Wno-pedantic;-Wno-unused-parameter"
)
