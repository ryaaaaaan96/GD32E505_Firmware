# 通信机制只依赖 aOS/aLib；具体协议通过数组回调注册。
add_library(aComm STATIC src/aComm.c src/aComm_channel.c
    src/aComm_endpoint.c src/aComm_table.c src/aComm_binding.c)
target_include_directories(aComm PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(aComm PUBLIC aOS PRIVATE aclass_project_options)
foreach(feature STATIC DYNAMIC)
    target_compile_definitions(aComm PUBLIC
        ACOMM_${feature}_ENABLE=$<BOOL:${ACOMM_${feature}_ENABLE}>)
endforeach()
