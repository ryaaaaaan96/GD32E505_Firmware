# aDrv modules enabled by this firmware project.
#
# This is the only project-level source of peripheral selection.  The selected
# GD32 port uses these values both to generate gd32e50x_libopt.h and to choose
# the matching aDrv/SPL source files.

para_set(ADRV_MODULE_GPIO  1)
para_set(ADRV_MODULE_USART 1)
para_set(ADRV_USART_INTERRUPT 1)
para_set(ADRV_USART_ASYNC     0)
para_set(ADRV_MODULE_DMA   0)
para_set(ADRV_MODULE_SPI   0)
para_set(ADRV_MODULE_QSPI  0)
