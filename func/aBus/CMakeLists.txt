add_library(aBusTable INTERFACE)
target_include_directories(aBusTable INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(aBusTable INTERFACE aLib)
if(NOT ABUS_ENABLE)
    return()
endif()

if(NOT ABUS_LOCK_GRANULARITY MATCHES "^(NONE|BUS|SIG)$")
    message(FATAL_ERROR "ABUS_LOCK_GRANULARITY must be NONE, BUS or SIG")
endif()

if(NOT DEFINED ABUS_DEF_CHECK_ENABLE OR
   NOT "${ABUS_DEF_CHECK_ENABLE}" MATCHES "^(ON|OFF|TRUE|FALSE|0|1)$")
    message(FATAL_ERROR "ABUS_DEF_CHECK_ENABLE must be boolean")
endif()

foreach(option ABUS_STATIC_ENABLE ABUS_DYNAMIC_ENABLE)
    if(NOT DEFINED ${option} OR
       NOT "${${option}}" MATCHES "^(ON|OFF|TRUE|FALSE|0|1)$")
        message(FATAL_ERROR "${option} must be boolean")
    endif()
endforeach()
if(NOT ABUS_STATIC_ENABLE AND NOT ABUS_DYNAMIC_ENABLE)
    message(FATAL_ERROR "aBus requires STATIC or DYNAMIC allocation enabled")
endif()
add_library(aBus STATIC src/aBus.c)
target_compile_definitions(aBus PUBLIC
    ABUS_STATIC_ENABLE=$<BOOL:${ABUS_STATIC_ENABLE}>
    ABUS_DYNAMIC_ENABLE=$<BOOL:${ABUS_DYNAMIC_ENABLE}>
)
target_compile_definitions(aBus PRIVATE
    ABUS_DEF_CHECK_ENABLE=$<BOOL:${ABUS_DEF_CHECK_ENABLE}>
)
target_link_libraries(aBus PUBLIC aBusTable PRIVATE aclass_project_options)
if(NOT ABUS_LOCK_GRANULARITY STREQUAL "NONE")
    # Public instance/state storage uses the opaque aOS mutex type.
    target_link_libraries(aBus PUBLIC aOS)
elseif(ABUS_DYNAMIC_ENABLE)
    target_link_libraries(aBus PRIVATE aOS)
endif()
if(ABUS_LOCK_GRANULARITY STREQUAL "NONE")
    target_compile_definitions(aBus PUBLIC ABUS_LOCK_MODE=0)
elseif(ABUS_LOCK_GRANULARITY STREQUAL "SIG")
    target_compile_definitions(aBus PUBLIC ABUS_LOCK_MODE=2)
else()
    target_compile_definitions(aBus PUBLIC ABUS_LOCK_MODE=1)
endif()
