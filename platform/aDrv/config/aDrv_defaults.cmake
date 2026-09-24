# Defaults used by aDrv when a project omits a module from its config file.
# These are normal variables so each CMake configure follows source config.
foreach(module GPIO USART DMA SPI QSPI)
    if(NOT DEFINED ADRV_MODULE_${module})
        set(ADRV_MODULE_${module} 0)
    endif()
endforeach()

foreach(feature USART_INTERRUPT USART_ASYNC)
    if(NOT DEFINED ADRV_${feature})
        set(ADRV_${feature} 0)
    endif()
endforeach()
