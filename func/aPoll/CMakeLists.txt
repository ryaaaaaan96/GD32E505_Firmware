add_library(aPoll STATIC aPoll.c)
target_include_directories(aPoll PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(aPoll PUBLIC aOS PRIVATE aclass_project_options)
foreach(feature STATIC DYNAMIC)
    target_compile_definitions(aPoll PUBLIC
        APOLL_${feature}_ENABLE=$<BOOL:${APOLL_${feature}_ENABLE}>)
endforeach()
