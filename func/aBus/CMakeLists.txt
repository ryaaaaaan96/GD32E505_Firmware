if(NOT ABUS_LOCK_GRANULARITY MATCHES "^(NONE|BUS|SIG)$")
    message(FATAL_ERROR "ABUS_LOCK_GRANULARITY must be NONE, BUS or SIG")
endif()

if(NOT DEFINED ABUS_DEF_CHECK_ENABLE OR
   NOT "${ABUS_DEF_CHECK_ENABLE}" MATCHES "^(ON|OFF|TRUE|FALSE|0|1)$")
    message(FATAL_ERROR "ABUS_DEF_CHECK_ENABLE must be boolean")
endif()

add_library(aBus STATIC src/aBus.c)
target_compile_definitions(aBus PRIVATE
    ABUS_DEF_CHECK_ENABLE=$<BOOL:${ABUS_DEF_CHECK_ENABLE}>
)
target_include_directories(aBus PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(aBus PUBLIC aLib PRIVATE aclass_project_options)
if(ABUS_LOCK_GRANULARITY STREQUAL "SIG")
    # The public state contains aOSMutex_t only in this configuration.
    target_link_libraries(aBus PUBLIC aOS)
elseif(ABUS_LOCK_GRANULARITY STREQUAL "BUS")
    target_link_libraries(aBus PRIVATE aOS)
endif()
if(ABUS_LOCK_GRANULARITY STREQUAL "NONE")
    target_compile_definitions(aBus PUBLIC ABUS_LOCK_MODE=0)
elseif(ABUS_LOCK_GRANULARITY STREQUAL "SIG")
    target_compile_definitions(aBus PUBLIC ABUS_LOCK_MODE=2)
else()
    target_compile_definitions(aBus PUBLIC ABUS_LOCK_MODE=1)
endif()
