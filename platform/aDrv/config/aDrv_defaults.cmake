# Hardware-independent aDrv module defaults.  The project-level
# config/aDrv_config.cmake overrides these values with para_set().

set(ADRV_MODULE_GPIO  0 CACHE BOOL "Enable the aDrv GPIO module" FORCE)
set(ADRV_MODULE_USART 0 CACHE BOOL "Enable the aDrv USART module" FORCE)
set(ADRV_USART_INTERRUPT 0 CACHE BOOL "Enable interrupt-driven USART" FORCE)
set(ADRV_USART_ASYNC 0 CACHE BOOL "Enable asynchronous USART using DMA" FORCE)
set(ADRV_MODULE_DMA   0 CACHE BOOL "Enable the aDrv DMA module" FORCE)
set(ADRV_MODULE_SPI   0 CACHE BOOL "Enable the aDrv SPI module" FORCE)
set(ADRV_MODULE_QSPI  0 CACHE BOOL "Enable the aDrv QSPI module" FORCE)
