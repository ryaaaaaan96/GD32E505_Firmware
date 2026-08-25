add_library(aRS485 STATIC aDev_rs485.c)
target_include_directories(aRS485 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aRS485 PUBLIC aDevUsart aDrv)
