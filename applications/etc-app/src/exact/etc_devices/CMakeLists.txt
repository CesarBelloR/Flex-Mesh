target_include_directories(app PRIVATE .)
target_sources(app PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/etc_device.c
        ${CMAKE_CURRENT_SOURCE_DIR}/etc_device_record.c
        ${CMAKE_CURRENT_SOURCE_DIR}/etc_device_helper.c
)