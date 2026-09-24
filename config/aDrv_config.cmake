# aDrv modules enabled by this firmware project.
#
# This file is the project-level source of peripheral selection. The selected
# GD32 port uses these values both to generate gd32e50x_libopt.h and to choose
# the matching aDrv/SPL source files.

set(ADRV_MODULE_GPIO 1)
set(ADRV_MODULE_USART 1)
set(ADRV_USART_INTERRUPT 1)
set(ADRV_USART_ASYNC 1)
set(ADRV_MODULE_DMA 0)
set(ADRV_MODULE_SPI 0)
set(ADRV_MODULE_QSPI 0)
