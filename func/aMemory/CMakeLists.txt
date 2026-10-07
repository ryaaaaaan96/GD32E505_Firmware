add_library(aMemory STATIC aMemory.c)
target_include_directories(aMemory PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(aMemory PUBLIC aLib PRIVATE aclass_project_options)
target_compile_definitions(aMemory PUBLIC
    AMEMORY_MAX_PARTITIONS=${AMEMORY_MAX_PARTITIONS})
