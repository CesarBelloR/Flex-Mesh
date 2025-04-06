target_include_directories(app PRIVATE 
        .
        ${CMAKE_SOURCE_DIR}/src/modules
)

target_sources(app PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/etc_calibration.c
)