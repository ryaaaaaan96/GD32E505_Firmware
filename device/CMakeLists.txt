if(ADEV_FLASH25Q_ENABLED)
    add_subdirectory(aDev_Flash25q)
endif()

if(ADEV_USART_ENABLED)
    add_subdirectory(aDev_usart)
endif()

if(ADEV_LED_ENABLED)
    add_subdirectory(aDev_LED)
endif()
