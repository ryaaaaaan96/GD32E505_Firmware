# 通信机制只依赖 aOS/aLib；具体协议由应用通过回调注册。
add_library(aComm STATIC src/aComm.c src/aComm_channel.c src/aComm_endpoint.c
    src/aComm_mapping.c src/aComm_mapping_data.c
    src/aComm_mapping_client.c src/aComm_mapping_server.c
    src/aComm_mapping_plan.c)
target_include_directories(aComm PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(aComm PUBLIC aOS PRIVATE aclass_project_options)
foreach(feature STATIC DYNAMIC)
    target_compile_definitions(aComm PUBLIC
        ACOMM_${feature}_ENABLE=$<BOOL:${ACOMM_${feature}_ENABLE}>)
endforeach()
