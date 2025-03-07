target_include_directories(app PRIVATE .)
target_sources(app PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/etc_sensor.c
    ${CMAKE_CURRENT_SOURCE_DIR}/etc_sensor_helper.c
)