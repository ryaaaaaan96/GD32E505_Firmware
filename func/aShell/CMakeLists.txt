add_library(aShell STATIC src/aShell_config.c)

# Reusable builds may omit the product override.
if(NOT DEFINED ASHELL_HISTORY_COUNT)
    set(ASHELL_HISTORY_COUNT 10)
endif()
if(NOT "${ASHELL_HISTORY_COUNT}" MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "ASHELL_HISTORY_COUNT must be an integer in [1, 255]")
endif()
if(ASHELL_HISTORY_COUNT GREATER 255)
    message(FATAL_ERROR "ASHELL_HISTORY_COUNT must be an integer in [1, 255]")
endif()
target_compile_definitions(aShell PRIVATE
    ASHELL_HISTORY_COUNT=${ASHELL_HISTORY_COUNT}
)

if(ASHELL_ENABLE)
    if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "aShell section export currently requires GCC")
    endif()
    # Product linker script collects the exported command sections.
    target_sources(aShell PRIVATE
        src/aShell.c
        src/aShell_output.c
        src/aShell_nr.c
        nr_micro_shell/src/nr_micro_shell_core.c
    )
    target_include_directories(aShell PRIVATE
        port
        nr_micro_shell/inc
    )
    target_link_libraries(aShell PRIVATE aOS)
else()
    target_sources(aShell PRIVATE src/aShell_stub.c)
endif()

target_compile_definitions(aShell PUBLIC
    ASHELL_ENABLE=$<BOOL:${ASHELL_ENABLE}>
)
target_include_directories(aShell
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
# All sources, including the vendor backend, inherit project build options.
# No module-local warning suppression or optimization overrides.
target_link_libraries(aShell PUBLIC aLib PRIVATE aclass_project_options)
