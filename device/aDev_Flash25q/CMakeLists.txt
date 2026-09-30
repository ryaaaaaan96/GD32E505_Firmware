# Upstream is compiled without editing or including its example port/config.
add_library(aFlash25qSfud OBJECT
    SFUD/sfud/src/sfud.c
    SFUD/sfud/src/sfud_sfdp.c
)
target_include_directories(aFlash25qSfud PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/config"
    "${CMAKE_CURRENT_SOURCE_DIR}/SFUD/sfud/inc"
)
target_link_libraries(aFlash25qSfud PRIVATE aclass_project_options)

add_library(aFlash25q STATIC
    aDev_flash25q.c
    port/aDev_flash25q_sfud.c
    port/aDev_flash25q_spi.c
)
target_include_directories(aFlash25q PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(aFlash25q
    PUBLIC aDrv aLib aFlash25qSfud
    PRIVATE aOS aclass_project_options
)
foreach(feature STATIC DYNAMIC)
    target_compile_definitions(aFlash25q PUBLIC
        ADEV_FLASH25Q_${feature}_ENABLE=$<BOOL:${ADEV_FLASH25Q_${feature}_ENABLE}>
    )
endforeach()
