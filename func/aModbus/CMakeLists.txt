# 官方源码保持不变，直接继承项目参数，不加载上游工程 CMake。
# 协议和映射依赖 aComm 契约；aComm 不反向依赖协议模块。
add_library(aModbus STATIC
    src/aModbus.c
    src/aModbus_client.c
    src/aModbus_server.c
    src/aModbus_endpoint.c
    src/aModbus_mapping.c
    src/aModbus_mapping_data.c
    nanoMODBUS/nanomodbus.c
)
target_include_directories(aModbus PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
# 仅静态实例布局需要向调用方传播上游头与裁剪宏。
set(nanomodbus_visibility PRIVATE)
if(AMODBUS_STATIC_ENABLE)
    set(nanomodbus_visibility PUBLIC)
endif()
target_include_directories(aModbus ${nanomodbus_visibility}
    "${CMAKE_CURRENT_SOURCE_DIR}/nanoMODBUS")
target_link_libraries(aModbus PUBLIC aOS aComm
    PRIVATE aclass_project_options)
foreach(feature STATIC DYNAMIC CLIENT SERVER)
    target_compile_definitions(aModbus PUBLIC
        AMODBUS_${feature}_ENABLE=$<BOOL:${AMODBUS_${feature}_ENABLE}>
    )
endforeach()
if(NOT AMODBUS_CLIENT_ENABLE)
    target_compile_definitions(aModbus ${nanomodbus_visibility}
        NMBS_CLIENT_DISABLED)
endif()
if(NOT AMODBUS_SERVER_ENABLE)
    target_compile_definitions(aModbus ${nanomodbus_visibility}
        NMBS_SERVER_DISABLED)
endif()
# 首版仅暴露基础功能码，裁剪暂不提供的从站功能。
target_compile_definitions(aModbus ${nanomodbus_visibility}
    NMBS_SERVER_READ_FILE_RECORD_DISABLED
    NMBS_SERVER_WRITE_FILE_RECORD_DISABLED
    NMBS_SERVER_READ_WRITE_REGISTERS_DISABLED
    NMBS_SERVER_READ_DEVICE_IDENTIFICATION_DISABLED
)
