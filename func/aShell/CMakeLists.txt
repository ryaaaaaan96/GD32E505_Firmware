add_library(
    aShell
    STATIC
)

if(ASHELL_ENABLED)
    set(LETTER_SHELL_SOURCES
        src/shell.c
        src/shell_ext.c
        src/shell_cmd_list.c
    )

    target_sources(
        aShell
        PRIVATE
            aShell.c
            ${LETTER_SHELL_SOURCES}
    )

    target_compile_definitions(
        aShell
        PRIVATE
            SHELL_CFG_USER="shell_cfg_user.h"
    )

    target_link_libraries(
        aShell
        PRIVATE
            aOS
    )

    set_source_files_properties(
        ${LETTER_SHELL_SOURCES}
        PROPERTIES
            COMPILE_OPTIONS
                "-Wno-error;-Wno-pedantic;-Wno-cast-function-type;-Wno-sign-compare;-Wno-strict-aliasing"
    )
else()
    target_sources(
        aShell
        PRIVATE
            aShell_stub.c
    )
endif()

target_compile_definitions(
    aShell
    PUBLIC
        ASHELL_ENABLED=$<BOOL:${ASHELL_ENABLED}>
)

target_include_directories(
    aShell
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(
    aShell
    PUBLIC
        aLib
)
