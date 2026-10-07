# 只编译 FlashDB KV/TSDB 核心，使用项目维护的 aMemory 存储补丁。
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/FlashDB/src/fdb.c" flashdb_source)
if(NOT flashdb_source MATCHES "FDB_USING_AMEMORY_MODE")
    message(FATAL_ERROR
        "Apply aDataBase/patches/0001-amemory.patch to FlashDB first")
endif()
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/FlashDB/src/fdb_kvdb.c" kvdb_source)
if(NOT kvdb_source MATCHES "fdb_kvdb_cache_prepare")
    message(FATAL_ERROR
        "Apply aDataBase/patches/0002-kv-index.patch to FlashDB first")
endif()
add_library(aDataBaseFlashDb OBJECT
    FlashDB/src/fdb.c
    FlashDB/src/fdb_kvdb.c
    FlashDB/src/fdb_tsdb.c
    FlashDB/src/fdb_utils.c
)
target_include_directories(aDataBaseFlashDb PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/config"
    "${CMAKE_CURRENT_SOURCE_DIR}/FlashDB/inc"
)
target_link_libraries(aDataBaseFlashDb PUBLIC aMemory
    PRIVATE aclass_project_options)

add_library(aDataBase STATIC
    aDataBase.c aDataBase_index.c port/memory_port.c)
# 仅复用 aBus 只读定义；不要求创建 aBus RAM 实例或启用运行库。
target_include_directories(aDataBase PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(aDataBase PUBLIC aBusTable
    PRIVATE aMemory aDataBaseFlashDb aOS aclass_project_options)
# 只有静态布局头需要上游类型；动态业务接口不传播 FlashDB 配置。
if(ADATABASE_STATIC_ENABLE)
    target_include_directories(aDataBase PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/config"
        "${CMAKE_CURRENT_SOURCE_DIR}/FlashDB/inc")
    target_link_libraries(aDataBase PUBLIC aMemory)
endif()
foreach(feature STATIC DYNAMIC)
    target_compile_definitions(aDataBase PUBLIC
        ADATABASE_${feature}_ENABLE=$<BOOL:${ADATABASE_${feature}_ENABLE}>
    )
endforeach()
