add_library(aDevUsart STATIC aDev_usart.c)
target_include_directories(aDevUsart PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aDevUsart PUBLIC aDrv PRIVATE aLib aOS)
