add_library(aShell STATIC src/aShell_config.c)

if(ASHELL_ENABLED)
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
    ASHELL_ENABLED=$<BOOL:${ASHELL_ENABLED}>
)
target_include_directories(aShell
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)
# All sources, including the vendor backend, inherit project build options.
# No module-local warning suppression or optimization overrides.
target_link_libraries(aShell PUBLIC aLib PRIVATE aclass_project_options)
