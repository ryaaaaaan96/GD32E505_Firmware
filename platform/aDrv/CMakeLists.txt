set(ADRV_PORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/port/${MCU_PORT}")
if(NOT EXISTS "${ADRV_PORT_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "aDrv port does not exist or has no CMakeLists.txt: ${ADRV_PORT_DIR}")
endif()

# Driver modules have cache-backed defaults and one project-level override.
include("${CMAKE_CURRENT_SOURCE_DIR}/config/aDrv_defaults.cmake")
set(ADRV_CONFIG "${CMAKE_SOURCE_DIR}/config/aDrv_config.cmake"
    CACHE FILEPATH "aDrv module configuration")
if(NOT EXISTS "${ADRV_CONFIG}")
    message(FATAL_ERROR "aDrv configuration does not exist: ${ADRV_CONFIG}")
endif()
include("${ADRV_CONFIG}")

# The selected port owns all MCU-specific sources and its vendor SDK subset.
add_subdirectory("${ADRV_PORT_DIR}" "port/${MCU_PORT}")

add_library(aDrv STATIC
    src/aDrv.c
    ${ADRV_PORT_SOURCES}
)

target_include_directories(aDrv
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${ADRV_PORT_DIR}"
)

target_compile_definitions(aDrv PRIVATE
    ADRV_MCU_NAME="${MCU_DEVICE}"
    ADRV_CAP_GPIO_PORT_COUNT=${MCU_GPIO_PORT_COUNT}
    ADRV_CAP_USART_COUNT=${MCU_USART_COUNT}
    ADRV_CAP_ADC_COUNT=${MCU_ADC_COUNT}
    ADRV_CAP_SPI_COUNT=${MCU_SPI_COUNT}
    ADRV_CAP_DMA_CHANNEL_COUNT=${MCU_DMA_CHANNEL_COUNT}
    ADRV_CAP_QSPI_COUNT=${MCU_QSPI_COUNT}
)

target_link_libraries(aDrv
    PUBLIC aLib aCore
    PRIVATE ${ADRV_PORT_VENDOR_TARGET}
)
